// Copyright 2017-2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Thomas McCabe <tom.mccabe@missionsystems.com.au>
//   Jared Silbermann <jared.silbermann@missionsystems.com.au>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>     // for remove_if
#include <deque>         // for deque
#include <limits>        // for numeric_...
#include <list>          // for operator!=
#include <memory>        // for allocator
#include <type_traits>   // for __decay_...
#include <unordered_map> // for unordere...
#include <utility>       // for pair

#include <boost/function.hpp>                   // for function
#include <boost/signals2/signal.hpp>            // for mutex
#include <boost/smart_ptr/shared_ptr.hpp>       // for shared_ptr
#include <boost/units/systems/si/frequency.hpp> // for frequency
#include <google/protobuf/descriptor.h>         // for Descriptor

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/acomms/acomms_constants.h"                  // for BROADCAS...
#include "goby/acomms/bind.h"                              // for bind
#include "goby/acomms/modemdriver/benthos_atm900_driver.h" // for BenthosA...
#include "goby/acomms/modemdriver/driver_exception.h"      // for ModemDri...
#include "goby/acomms/modemdriver/iridium_driver.h"        // for IridiumD...
#include "goby/acomms/modemdriver/iridium_shore_driver.h"  // for IridiumS...
#include "goby/acomms/modemdriver/mm_driver.h"             // for MMDriver
#include "goby/acomms/modemdriver/store_server_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"             // for UDPDriver
#include "goby/acomms/modemdriver/udp_multicast_driver.h"   // for UDPMulti...
#include "goby/acomms/protobuf/buffer.pb.h"                 // for DynamicB...
#include "goby/acomms/protobuf/driver_base.pb.h"            // for DriverCo...
#include "goby/acomms/protobuf/modem_message.pb.h"          // for ModemTra...
#include "goby/exception.h"                                 // for Exception
#include "goby/middleware/protobuf/transporter_config.pb.h" // for Transpor...
#include "goby/middleware/transport/intervehicle/groups.h"  // for metadata...
#include "goby/middleware/transport/publisher.h"            // for Publisher
#include "goby/util/debug_logger/flex_ostreambuf.h"         // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"     // for operator<<
#include "goby/util/debug_logger/term_color.h"              // for Colors
#ifdef ENABLE_JANUS_ACOMMS
#include "goby/acomms/modemdriver/janus_driver.h" // for JanusDriver...
#endif
#include "driver_thread.h"
#include "goby/acomms/modemdriver/popoto_driver.h" // for PopotoDr...

using goby::glog;
using namespace goby::util::logger;
using goby::middleware::protobuf::SerializerTransporterMessage;

std::map<std::string, void*> load_plugins()
{
    std::map<std::string, void*> driver_plugins;

    // load plugins from environmental variable
    std::string s_plugins;
    char* legacy_plugins = getenv("PACOMMSHANDLER_PLUGINS");
    if (legacy_plugins)
        s_plugins = std::string(legacy_plugins);

    char* plugins = getenv("GOBY_MODEMDRIVER_PLUGINS");
    if (plugins)
    {
        if (!s_plugins.empty())
            s_plugins += ";";
        s_plugins += std::string(plugins);
    }

    if (!s_plugins.empty())
    {
        std::vector<std::string> plugin_vec;
        boost::split(plugin_vec, s_plugins, boost::is_any_of(";:,"));

        for (auto& i : plugin_vec)
        {
            std::vector<void*> plugin_handles_;

            // RTLD_NODELETE: keep the library mapped for the life of the process (even if
            // dlclose() is called) as the protobuf descriptor pools may reference memory
            // (e.g. custom options extensions) owned by this library at static destruction
            void* handle = dlopen(i.c_str(), RTLD_LAZY | RTLD_NODELETE);

            if (handle)
                plugin_handles_.push_back(handle);
            else
            {
                std::cerr << "Failed to open ModemDriver plugin library: " << i << std::endl;
                exit(EXIT_FAILURE);
            }

            const auto name_function = (const char* (*)(void))dlsym(handle, "goby_driver_name");
            if (name_function)
            {
                driver_plugins.insert(std::make_pair(std::string((*name_function)()), handle));
            }
            else
            {
                std::cerr << "Library must define \"goby_driver_name()\" as extern \"C\":" << i
                          << std::endl;
            }
        }
    }

    return driver_plugins;
}

// Driver plugin map singleton - shared across all ModemDriverThread template instantiations
static std::map<std::string, void*>& get_driver_plugins()
{
    static std::map<std::string, void*> plugins = load_plugins();
    return plugins;
}

// Require "eager" init of plugins so that Protobuf extensions are loaded early
static const auto plugins_initialized = []
{
    get_driver_plugins();
    return true;
}();

template <typename ImplementationTag>
goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::ModemDriverThread(
    const intervehicle::protobuf::PortalConfig::LinkConfig& config)
    : goby::middleware::Thread<intervehicle::protobuf::PortalConfig::LinkConfig,
                               InterProcessForwarder<InterThreadTransporter, ImplementationTag>>(
          config, 10 * boost::units::si::hertz),
      buffer_(this->cfg().modem_id()),
      mac_(this->cfg().modem_id()),
      glog_group_("goby::middleware::intervehicle::driver_thread::" +
                  goby::acomms::ModemDriverBase::driver_name(this->cfg().driver())),
      next_modem_report_time_(goby::time::SteadyClock::now()),
      modem_report_interval_(goby::time::convert_duration<goby::time::SteadyClock::duration>(
          this->cfg().modem_report_interval_with_units()))
{
    goby::glog.add_group(glog_group_, util::Colors::blue);
    interthread_ = std::make_unique<InterThreadTransporter>();
    interprocess_ =
        std::make_unique<InterProcessForwarder<InterThreadTransporter, ImplementationTag>>(
            *interthread_);
    this->set_transporter(interprocess_.get());

    interprocess_->template subscribe<groups::modem_data_out, SerializerTransporterMessage>(
        [this](std::shared_ptr<const SerializerTransporterMessage> msg)
        { _buffer_message(std::move(msg)); });

    interprocess_
        ->template subscribe<groups::modem_subscription_forward_tx,
                             intervehicle::protobuf::Subscription, MarshallingScheme::PROTOBUF>(
            [this](const std::shared_ptr<const intervehicle::protobuf::Subscription>& subscription)
            { _forward_subscription(*subscription); });

    interprocess_
        ->template subscribe<groups::modem_subscription_forward_rx,
                             intervehicle::protobuf::Subscription, MarshallingScheme::PROTOBUF>(
            [this](const std::shared_ptr<const intervehicle::protobuf::Subscription>& subscription)
            { _accept_subscription(*subscription); });

    _create_driver();

    mac_.signal_initiate_transmission.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& msg)
        {
            protobuf::ModemTransmissionWithLinkID msg_with_id;
            msg_with_id.set_link_modem_id(this->cfg().modem_id());
            *msg_with_id.mutable_data() = msg;
            interprocess_->template publish<groups::mac_initiate_transmission>(msg_with_id);
        });

    mac_.signal_slot_start.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& msg)
        {
            protobuf::ModemTransmissionWithLinkID msg_with_id;
            msg_with_id.set_link_modem_id(this->cfg().modem_id());
            *msg_with_id.mutable_data() = msg;
            interprocess_->template publish<groups::mac_slot_start>(msg_with_id);
        });

    mac_.startup(this->cfg().mac());

    subscription_key_.set_marshalling_scheme(MarshallingScheme::DCCL);
    subscription_key_.set_type(intervehicle::protobuf::Subscription::descriptor()->full_name());
    subscription_key_.set_group_numeric(
        goby::middleware::intervehicle::groups::subscription_forward.numeric());

    _try_driver_startup();

    // a driver waiting to restart must not hold up the portal's other links
    goby::glog.is_debug1() && goby::glog << group(glog_group_) << "Driver ready" << std::endl;
    interthread_->template publish<groups::modem_driver_ready, bool>(true);
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_create_driver()
{
    if (this->cfg().driver().has_driver_name())
    {
        std::map<std::string, void*>::const_iterator driver_it =
            get_driver_plugins().find(this->cfg().driver().driver_name());

        if (driver_it == get_driver_plugins().end())
            glog.is_die() && glog << "Could not find driver_plugin_name '"
                                  << this->cfg().driver().driver_name()
                                  << "'. Make sure it is loaded using the GOBY_MODEMDRIVER_PLUGINS "
                                     "environmental var"
                                  << std::endl;
        else
        {
            auto driver_function = (goby::acomms::ModemDriverBase * (*)(void))
                dlsym(driver_it->second, "goby_make_driver");

            if (!driver_function)
            {
                glog.is(DIE) && glog << "Could not load goby::acomms::ModemDriverBase* "
                                        "goby_make_driver() for driver name '"
                                     << this->cfg().driver().driver_name() << "'." << std::endl;
            }
            else
            {
                driver_.reset((*driver_function)());
            }
        }
    }
    else
    {
        switch (this->cfg().driver().driver_type())
        {
            case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
                driver_ = std::make_unique<goby::acomms::MMDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_IRIDIUM:
                driver_ = std::make_unique<goby::acomms::IridiumDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_UDP:
                driver_ = std::make_unique<goby::acomms::UDPDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_UDP_MULTICAST:
                driver_ = std::make_unique<goby::acomms::UDPMulticastDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
                driver_ = std::make_unique<goby::acomms::IridiumShoreDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_BENTHOS_ATM900:
                driver_ = std::make_unique<goby::acomms::BenthosATM900Driver>();
                break;

            case goby::acomms::protobuf::DRIVER_STORE_SERVER:
                driver_ = std::make_unique<goby::acomms::StoreServerDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_POPOTO:
                driver_ = std::make_unique<goby::acomms::PopotoDriver>();
                break;

#ifdef ENABLE_JANUS_ACOMMS
            case goby::acomms::protobuf::DRIVER_JANUS:
                driver_ = std::make_unique<goby::acomms::JanusDriver>();
                break;
#endif
            case goby::acomms::protobuf::DRIVER_NONE:
            case goby::acomms::protobuf::DRIVER_ABC_EXAMPLE_MODEM:
            case goby::acomms::protobuf::DRIVER_UFIELD_SIM_DRIVER:
            case goby::acomms::protobuf::DRIVER_BLUEFIN_MOOS:
                throw(goby::Exception(
                    "Unsupported driver type: " +
                    goby::acomms::protobuf::DriverType_Name(this->cfg().driver().driver_type())));
                break;
            default:
                throw(goby::Exception(
                    "Please specify a supported driver type: " +
                    goby::acomms::protobuf::DriverType_Name(this->cfg().driver().driver_type())));
                break;
        }
    }

    driver_->signal_receive.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& rx_msg)
        {
            protobuf::ModemTransmissionWithLinkID msg_with_id;
            msg_with_id.set_link_modem_id(this->cfg().modem_id());
            *msg_with_id.mutable_data() = rx_msg;
            interprocess_->template publish<groups::modem_receive>(msg_with_id);
        });

    driver_->signal_transmit_result.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& tx_msg)
        {
            protobuf::ModemTransmissionWithLinkID msg_with_id;
            msg_with_id.set_link_modem_id(this->cfg().modem_id());
            *msg_with_id.mutable_data() = tx_msg;
            interprocess_->template publish<groups::modem_transmit_result>(msg_with_id);
        });

    driver_->signal_raw_incoming.connect(
        [&](const goby::acomms::protobuf::ModemRaw& msg)
        {
            protobuf::ModemRawWithLinkID msg_with_id;
            msg_with_id.set_link_modem_id(this->cfg().modem_id());
            *msg_with_id.mutable_data() = msg;
            interprocess_->template publish<groups::modem_raw_incoming>(msg_with_id);
        });

    driver_->signal_raw_outgoing.connect(
        [&](const goby::acomms::protobuf::ModemRaw& msg)
        {
            protobuf::ModemRawWithLinkID msg_with_id;
            msg_with_id.set_link_modem_id(this->cfg().modem_id());
            *msg_with_id.mutable_data() = msg;
            interprocess_->template publish<groups::modem_raw_outgoing>(msg_with_id);
        });

    driver_->signal_receive.connect([&](const goby::acomms::protobuf::ModemTransmission& rx_msg)
                                    { _receive(rx_msg); });

    driver_->signal_data_request.connect([&](goby::acomms::protobuf::ModemTransmission* msg)
                                         { this->_data_request(msg); });

    goby::acomms::bind(mac_, *driver_);
}

template <typename ImplementationTag>
bool goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_try_driver_startup()
{
    auto driver_cfg = this->cfg().driver();
    driver_cfg.set_modem_id(_id_within_subnet(this->cfg().driver().modem_id()));
    try
    {
        driver_->startup(driver_cfg);
    }
    catch (const goby::acomms::ModemDriverException& e)
    {
        if (!_restart_on_failure(e))
            throw;
        return false;
    }

    if (failure_status_ != goby::acomms::protobuf::ModemDriverStatus::NOMINAL)
        goby::glog.is_warn() && goby::glog << group(glog_group_)
                                           << "Driver started up after previous failure."
                                           << std::endl;
    driver_started_ = true;
    failure_status_ = goby::acomms::protobuf::ModemDriverStatus::NOMINAL;
    return true;
}

template <typename ImplementationTag>
bool goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_restart_on_failure(
    const goby::acomms::ModemDriverException& e)
{
    if (!this->cfg().has_restart_on_failure() ||
        e.status() == goby::acomms::protobuf::ModemDriverStatus::INVALID_CONFIGURATION)
        return false;

    goby::glog.is_warn() &&
        goby::glog << group(glog_group_) << "Driver failed (" << e.what() << "), restarting in "
                   << this->cfg().restart_on_failure().interval() << " s" << std::endl;

    // a fresh driver, as shutting down the failed one would talk to the modem
    goby::acomms::unbind(mac_, *driver_);
    driver_.reset();
    _create_driver();
    // values awaiting these acks stay in buffer_ and are resent after ack_timeout
    pending_ack_.clear();

    driver_started_ = false;
    failure_status_ = e.status();
    next_restart_attempt_ = goby::time::SteadyClock::now() +
                            goby::time::convert_duration<goby::time::SteadyClock::duration>(
                                this->cfg().restart_on_failure().interval_with_units());
    return true;
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::finalize()
{
    if (!driver_started_ || !this->cfg().shutdown_driver_on_exit())
        return;

    goby::glog.is_debug1() && goby::glog << group(glog_group_) << "Shutting down driver"
                                         << std::endl;
    try
    {
        driver_->shutdown();
    }
    catch (const goby::acomms::ModemDriverException& e)
    {
        goby::glog.is_warn() && goby::glog << group(glog_group_)
                                           << "Driver failed to shut down: " << e.what()
                                           << std::endl;
    }
    driver_started_ = false;
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_publish_modem_report()
{
    protobuf::ModemReportWithLinkID report_with_id;
    report_with_id.set_link_modem_id(this->cfg().modem_id());
    auto& report = *report_with_id.mutable_data();
    if (driver_started_)
    {
        driver_->report(&report);
    }
    else
    {
        report.set_modem_id(_id_within_subnet(this->cfg().driver().modem_id()));
        report.set_time_with_units(goby::time::SystemClock::now<goby::time::MicroTime>());
        report.set_link_state(goby::acomms::protobuf::ModemReport::LINK_NOT_AVAILABLE);
        report.set_driver_status(failure_status_);
    }
    interprocess_->template publish<groups::modem_report>(report_with_id);
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::loop()
{
    auto expired = buffer_.expire();

    if (!expired.empty())
    {
        auto now = goby::time::SteadyClock::now();
        for (const auto& value : expired)
            _expire_value(now, value,
                          intervehicle::protobuf::ExpireData::EXPIRED_TIME_TO_LIVE_EXCEEDED);
    }

    if (driver_started_ ||
        (goby::time::SteadyClock::now() >= next_restart_attempt_ && _try_driver_startup()))
    {
        try
        {
            driver_->do_work();
            mac_.do_work();
        }
        catch (const goby::acomms::ModemDriverException& e)
        {
            if (!_restart_on_failure(e))
                throw;
        }
    }

    auto now = goby::time::SteadyClock::now();
    if (now > next_modem_report_time_ + modem_report_interval_)
    {
        _publish_modem_report();
        // a restart attempt can block for longer than the interval; don't publish a burst after it
        next_modem_report_time_ = now;
    }
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_expire_value(
    const goby::time::SteadyClock::time_point now,
    const goby::acomms::DynamicBuffer<buffer_data_type>::Value& value,
    intervehicle::protobuf::ExpireData::ExpireReason reason)
{
    protobuf::ExpireMessagePair expire_pair;
    protobuf::ExpireData& expire_data = *expire_pair.mutable_data();
    expire_data.mutable_header()->set_src(goby::acomms::BROADCAST_ID);
    expire_data.mutable_header()->add_dest(value.modem_id);

    expire_data.set_latency_with_units(
        goby::time::convert_duration<goby::time::MicroTime>(now - value.push_time));
    expire_data.set_reason(reason);

    *expire_pair.mutable_serializer() = value.data;
    interprocess_->template publish<groups::modem_expire_in>(expire_pair);

    for (const auto& superseded : _take_superseded(value)) _expire_value(now, superseded, reason);
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_ack_value(
    intervehicle::protobuf::AckMessagePair& ack_pair, const goby::time::SteadyClock::time_point now,
    const goby::acomms::DynamicBuffer<buffer_data_type>::Value& value)
{
    ack_pair.mutable_data()->set_latency_with_units(
        goby::time::convert_duration<goby::time::MicroTime>(now - value.push_time));
    *ack_pair.mutable_serializer() = value.data;
    interprocess_->template publish<groups::modem_ack_in>(ack_pair);

    for (const auto& superseded : _take_superseded(value)) _ack_value(ack_pair, now, superseded);
}

template <typename ImplementationTag>
std::vector<typename goby::middleware::intervehicle::ModemDriverThread<
    ImplementationTag>::buffer_value_type>
goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_take_superseded(
    const buffer_value_type& value)
{
    std::vector<buffer_value_type> superseded;
    auto queued_it = queued_subscriptions_.find(value.modem_id);
    if (queued_it == queued_subscriptions_.end())
        return superseded;

    auto& queued = queued_it->second;
    auto it = std::find_if(queued.begin(), queued.end(),
                           [&](const auto& q) { return q.data == value.data; });
    if (it != queued.end())
    {
        superseded = std::move(it->superseded);
        queued.erase(it);
    }
    return superseded;
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_erase_pending_ack(
    const std::vector<goby::acomms::DynamicBuffer<buffer_data_type>::Value>& values)
{
    // otherwise a late ack would also report these values as delivered
    for (const auto& value : values)
    {
        for (auto& frame_values_p : pending_ack_)
        {
            auto& frame_values = frame_values_p.second;
            frame_values.erase(std::remove_if(frame_values.begin(), frame_values.end(),
                                              [&](const auto& pending)
                                              {
                                                  return pending.modem_id == value.modem_id &&
                                                         pending.subbuffer_id ==
                                                             value.subbuffer_id &&
                                                         pending.push_time == value.push_time &&
                                                         pending.data == value.data;
                                              }),
                               frame_values.end());
        }
    }
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_discard_superseded(
    const std::vector<goby::acomms::DynamicBuffer<buffer_data_type>::Value>& values)
{
    _erase_pending_ack(values);
    auto now = goby::time::SteadyClock::now();
    for (const auto& value : values)
        _expire_value(now, value, intervehicle::protobuf::ExpireData::EXPIRED_SUPERSEDED);
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_forward_subscription(
    intervehicle::protobuf::Subscription subscription)
{
    if (subscription.has_metadata())
        detail::DCCLSerializerParserHelperBase::load_metadata(subscription.metadata());

    if (subscription.intervehicle().broadcast())
        subscription.mutable_header()->set_src(_broadcast_id());
    else
        subscription.mutable_header()->set_src(this->cfg().driver().modem_id());

    for (auto dest : subscription.header().dest())
    {
        if (!_dest_is_in_subnet(dest))
            continue;

        auto buffer_id = _create_buffer_id(subscription_key_);
        if (!subscription_subbuffers_.count(dest))
        {
            auto subscription_buffer_cfg = this->cfg().subscription_buffer();
            if (!subscription_buffer_cfg.has_ack_required())
                subscription_buffer_cfg.set_ack_required(true);

            using value_base_type = std::invoke_result_t<
                decltype(&goby::acomms::protobuf::DynamicBufferConfig::value_base),
                const goby::acomms::protobuf::DynamicBufferConfig&>;

            // set subscriptions to maximum value
            if (!subscription_buffer_cfg.has_value_base())
                subscription_buffer_cfg.set_value_base(
                    std::numeric_limits<value_base_type>::has_infinity
                        ? std::numeric_limits<value_base_type>::infinity()
                        : std::numeric_limits<value_base_type>::max());

            buffer_.create(dest, buffer_id, subscription_buffer_cfg);
            subscription_subbuffers_.insert(dest);
        }

        glog.is_debug1() && glog << group(glog_group_) << "Forwarding subscription acoustically: "
                                 << _create_buffer_id(subscription) << std::endl;

        auto subscription_publication = serialize_publication(
            subscription, groups::subscription_forward, Publisher<decltype(subscription)>());

        // overwrite serialize_time to ensure mapping with InterVehicle portals
        auto subscribe_time = subscription.time_with_units();
        subscription_publication->mutable_key()->set_serialize_time_with_units(subscribe_time);

        auto& queued = queued_subscriptions_[dest];
        std::vector<buffer_value_type> superseded;
        auto equivalent_it =
            std::find_if(queued.begin(), queued.end(),
                         [&](const auto& q)
                         {
                             return q.subscription.dccl_id() == subscription.dccl_id() &&
                                    q.subscription.group() == subscription.group() &&
                                    q.subscription.intervehicle() == subscription.intervehicle();
                         });
        if (equivalent_it != queued.end())
        {
            auto erased = buffer_.erase_if(dest, buffer_id, [&](const auto& v)
                                           { return v.data == equivalent_it->data; });
            if (equivalent_it->subscription.action() == subscription.action())
            {
                // the new subscription stands in for the one it replaces (which may belong to
                // another local subscriber), so that one is acked or expired along with it
                _erase_pending_ack(erased);
                superseded = std::move(equivalent_it->superseded);
                superseded.insert(superseded.end(), erased.begin(), erased.end());
                queued.erase(equivalent_it);
            }
            else
            {
                // the opposite action cancels the queued subscription and whatever it stood in for
                erased.insert(erased.end(), equivalent_it->superseded.begin(),
                              equivalent_it->superseded.end());
                queued.erase(equivalent_it);
                _discard_superseded(erased);
            }
        }

        auto now = goby::time::SteadyClock::now();
        auto exceeded = buffer_.push({dest, buffer_id, now, *subscription_publication});
        queued.push_back({subscription, *subscription_publication, std::move(superseded)});
        for (const auto& value : exceeded)
            _expire_value(now, value, intervehicle::protobuf::ExpireData::EXPIRED_BUFFER_OVERFLOW);
    }
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_data_request(
    goby::acomms::protobuf::ModemTransmission* msg)
{
    // erase any pending acks with greater frame numbers (we never received these)
    auto it = pending_ack_.lower_bound(msg->frame_start()), end = pending_ack_.end();
    while (it != end)
    {
        goby::glog.is_debug1() && goby::glog << group(glog_group_) << "Erasing "
                                             << it->second.size() << " values not acked for frame "
                                             << it->first << std::endl;
        it = pending_ack_.erase(it);
    }

    int dest = msg->dest();
    for (auto frame_number = msg->frame_start(),
              total_frames = msg->max_num_frames() + msg->frame_start();
         frame_number < total_frames; ++frame_number)
    {
        std::string* frame = msg->add_frame();

        while (frame->size() < msg->max_frame_bytes())
        {
            try
            {
                auto buffer_value =
                    buffer_.top(dest, msg->max_frame_bytes() - frame->size(),
                                goby::time::convert_duration<std::chrono::microseconds>(
                                    this->cfg().ack_timeout_with_units()));
                dest = buffer_value.modem_id;
                *frame += buffer_value.data.data();

                bool ack_required = buffer_.sub(buffer_value.modem_id, buffer_value.subbuffer_id)
                                        .cfg()
                                        .ack_required();

                if (!ack_required)
                {
                    buffer_.erase(buffer_value);
                }
                else
                {
                    msg->set_ack_requested(true);
                    pending_ack_[frame_number].push_back(buffer_value);
                }
            }
            catch (goby::acomms::DynamicBufferNoDataException& e)
            {
                glog.is_debug1() && glog << group(glog_group_) << e.what() << std::endl;
                break;
            }
        }
    }

    if (!msg->has_ack_requested())
        msg->set_ack_requested(false);

    // convert src,dest to values with in subnet for modems that can't address large ids
    // e.g. 0x34 -> 0x04 for subnet mask 0xFFF0
    msg->set_src(_id_within_subnet(msg->src()));
    msg->set_dest(_id_within_subnet(dest));
}

template <typename ImplementationTag>
typename goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::subbuffer_id_type
goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_create_buffer_id(
    unsigned dccl_id, unsigned group)
{
    return "/group:" + std::to_string(group) + "/id:" + std::to_string(dccl_id) + "/";
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_accept_subscription(
    const intervehicle::protobuf::Subscription& subscription)
{
    auto buffer_id = _create_buffer_id(subscription);

    glog.is_debug2() &&
        glog << group(glog_group_) << "Received new forwarded subscription/unsubscription: "
             << subscription.ShortDebugString() << ", buffer_id: " << buffer_id << std::endl;

    auto dest = subscription.header().src();

    if (!_dest_is_in_subnet(dest))
        return;

    if (subscription.api_version() != GOBY_INTERVEHICLE_API_VERSION)
        return;

    switch (subscription.action())
    {
        case protobuf::Subscription::SUBSCRIBE:
        {
            bool is_new_cfg = true;
            auto it_pair = subscriber_buffer_cfg_[dest].equal_range(buffer_id);
            for (auto it = it_pair.first, end = it_pair.second; it != end; ++it)
            {
                if (it->second.intervehicle() == subscription.intervehicle())
                {
                    is_new_cfg = false;
                    break;
                }
            }

            if (is_new_cfg)
            {
                subscriber_buffer_cfg_[dest].insert(std::make_pair(buffer_id, subscription));
                _try_create_or_update_buffer(dest, buffer_id);
            }
            else
            {
                glog.is_debug2() &&
                    glog << group(glog_group_) << "Subscription configuration exists for "
                         << buffer_id << " with configuration: "
                         << subscription.intervehicle().ShortDebugString() << std::endl;
            }
        }
        break;
        case protobuf::Subscription::UNSUBSCRIBE:
        {
            auto it_pair = subscriber_buffer_cfg_[dest].equal_range(buffer_id);
            auto it_to_erase = subscriber_buffer_cfg_[dest].end();
            for (auto it = it_pair.first, end = it_pair.second; it != end; ++it)
            {
                if (it->second.intervehicle() == subscription.intervehicle())
                    it_to_erase = it;
            }

            if (it_to_erase != subscriber_buffer_cfg_[dest].end())
            {
                subscriber_buffer_cfg_[dest].erase(it_to_erase);
                if (!subscriber_buffer_cfg_[dest].count(buffer_id))
                {
                    subbuffers_created_[buffer_id].erase(dest);
                    buffer_.remove(dest, buffer_id);
                    glog.is_debug2() && glog << group(glog_group_)
                                             << "No more subscribers, removing buffer for "
                                             << buffer_id << std::endl;
                }
                else
                {
                    glog.is_debug2() && glog << group(glog_group_)
                                             << "Still more subscribers, not removing buffer for "
                                             << buffer_id << std::endl;
                    // update buffer configuration with remaining subscribers
                    _try_create_or_update_buffer(dest, buffer_id);
                }
            }
            else
            {
                glog.is_warn() && glog << group(glog_group_)
                                       << "No subscription configuration exists for " << buffer_id
                                       << std::endl;
            }
        }
        break;
    }
    // publish an update anyway, even if we didn't have to make any changes to inform subscribers to the subscription report than a subscription/unsubcription came in
    _publish_subscription_report(subscription);
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<
    ImplementationTag>::_try_create_or_update_buffer(modem_id_type dest_id,
                                                     const subbuffer_id_type& buffer_id)
{
    auto pub_it_pair = publisher_buffer_cfg_.equal_range(buffer_id);
    auto& dest_subscriber_buffer_cfg = subscriber_buffer_cfg_[dest_id];
    auto sub_it_pair = dest_subscriber_buffer_cfg.equal_range(buffer_id);

    if (pub_it_pair.first == pub_it_pair.second)
    {
        glog.is_debug2() &&
            glog << group(glog_group_)
                 << "No publisher yet for this subscription, buffer_id: " << buffer_id << std::endl;
    }
    else if (sub_it_pair.first == sub_it_pair.second)
    {
        glog.is_debug2() && glog << group(glog_group_)
                                 << "No subscriber yet for this subscription, buffer_id: "
                                 << buffer_id << std::endl;
    }
    else
    {
        std::vector<goby::acomms::protobuf::DynamicBufferConfig> cfgs;

        for (auto it = sub_it_pair.first, end = sub_it_pair.second; it != end; ++it)
            cfgs.push_back(it->second.intervehicle().buffer());
        for (auto it = pub_it_pair.first, end = pub_it_pair.second; it != end; ++it)
            cfgs.push_back(it->second.cfg().intervehicle().buffer());

        if (!subbuffers_created_[buffer_id].count(dest_id))
        {
            buffer_.create(dest_id, buffer_id, cfgs);
            subbuffers_created_[buffer_id].insert(dest_id);
            glog.is_debug2() && glog << group(glog_group_) << "Created buffer for dest: " << dest_id
                                     << " for id: " << buffer_id << " with " << cfgs.size()
                                     << " configurations" << std::endl;
        }
        else
        {
            buffer_.update(dest_id, buffer_id, cfgs);
            glog.is_debug2() && glog << group(glog_group_)
                                     << "Updated existing buffer for dest: " << dest_id
                                     << " for id: " << buffer_id << " with " << cfgs.size()
                                     << " configurations" << std::endl;
        }
    }
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_buffer_message(
    const std::shared_ptr<const SerializerTransporterMessage>& msg)
{
    if (msg->key().has_metadata())
        detail::DCCLSerializerParserHelperBase::load_metadata(msg->key().metadata());

    // check if we have this message loaded
    auto dccl_id = detail::DCCLSerializerParserHelperBase::id(msg->key().type());
    if (dccl_id == detail::DCCLSerializerParserHelperBase::INVALID_DCCL_ID)
    {
        // start sending metadata
        middleware::protobuf::SerializerMetadataRequest meta_request;
        *meta_request.mutable_key() = msg->key();
        meta_request.set_request(middleware::protobuf::SerializerMetadataRequest::METADATA_INCLUDE);
        interprocess_->template publish<groups::metadata_request>(meta_request);
        glog.is_warn() &&
            glog << group(glog_group_)
                 << "Omitting message because we don't have the DCCL metadata. Sending request: "
                 << meta_request.ShortDebugString() << std::endl;
        return;
    }
    else if (msg->key().has_metadata())
    {
        // stop sending metadata
        middleware::protobuf::SerializerMetadataRequest meta_request;
        *meta_request.mutable_key() = msg->key();
        // avoid extra data send
        meta_request.mutable_key()->clear_metadata();
        meta_request.set_request(middleware::protobuf::SerializerMetadataRequest::METADATA_EXCLUDE);
        glog.is_debug3() && glog << group(glog_group_)
                                 << "No need for more DCCL metadata. Sending request: "
                                 << meta_request.ShortDebugString() << std::endl;
        interprocess_->template publish<groups::metadata_request>(meta_request);
    }

    auto buffer_id = _create_buffer_id(dccl_id, msg->key().group_numeric());

    glog.is_debug3() && glog << group(glog_group_) << "Buffering message with id: " << buffer_id
                             << " from " << msg->ShortDebugString() << std::endl;

    bool is_new_cfg = true;
    auto it_pair = publisher_buffer_cfg_.equal_range(buffer_id);
    for (auto it = it_pair.first, end = it_pair.second; it != end; ++it)
    {
        if (it->second.cfg().intervehicle().buffer() == msg->key().cfg().intervehicle().buffer())
            is_new_cfg = false;
        break;
    }

    if (is_new_cfg)
    {
        publisher_buffer_cfg_.insert(std::make_pair(buffer_id, msg->key()));

        // check for new subbuffers from all existing subscribers
        for (const auto& sub_id_p : subscriber_buffer_cfg_)
        {
            auto dest_id = sub_id_p.first;
            _try_create_or_update_buffer(dest_id, buffer_id);
        }
    }

    if (!subbuffers_created_[buffer_id].empty())
    {
        // push to all subscribed buffers
        for (auto dest_id : subbuffers_created_[buffer_id])
        {
            if (!_dest_is_in_subnet(dest_id))
                continue;

            if (msg->key().cfg().intervehicle().clear_pending())
                _discard_superseded(buffer_.clear(dest_id, buffer_id));

            auto exceeded =
                buffer_.push({dest_id, buffer_id, goby::time::SteadyClock::now(), *msg});
            if (!exceeded.empty())
            {
                auto now = goby::time::SteadyClock::now();
                for (const auto& value : exceeded)
                    _expire_value(now, value,
                                  intervehicle::protobuf::ExpireData::EXPIRED_BUFFER_OVERFLOW);
            }
        }
    }
    else
    {
        auto now = goby::time::SteadyClock::now();
        _expire_value(now, {this->cfg().driver().modem_id(), buffer_id, now, *msg},
                      intervehicle::protobuf::ExpireData::EXPIRED_NO_SUBSCRIBERS);
    }
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::_receive(
    const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    glog.is(DEBUG1) && glog << group(glog_group_) << "Received: " << rx_msg.ShortDebugString()
                            << std::endl;

    int full_dest = _full_id(rx_msg.dest());
    int full_src = _full_id(rx_msg.src());

    if (rx_msg.type() == goby::acomms::protobuf::ModemTransmission::ACK)
    {
        if (full_dest != this->cfg().driver().modem_id())
        {
            glog.is(WARN) && glog << group(glog_group_)
                                  << "ignoring ack for modem_id = " << full_dest << std::endl;
            return;
        }
        for (int i = 0, n = rx_msg.acked_frame_size(); i < n; ++i)
        {
            int frame_number = rx_msg.acked_frame(i);
            if (!pending_ack_.count(frame_number))
            {
                glog.is(DEBUG1) && glog << group(glog_group_)
                                        << "got ack but we were not expecting one from " << full_src
                                        << " for frame " << frame_number << std::endl;
                continue;
            }
            else
            {
                protobuf::AckMessagePair ack_pair;
                protobuf::AckData& ack_data = *ack_pair.mutable_data();
                ack_data.mutable_header()->set_src(full_src);
                ack_data.mutable_header()->add_dest(full_dest);
                *ack_data.mutable_header()->mutable_modem_msg() = rx_msg;

                auto now = goby::time::SteadyClock::now();

                auto values_to_ack_it = pending_ack_.find(frame_number);
                glog.is(DEBUG1) && glog << group(glog_group_) << "processing "
                                        << values_to_ack_it->second.size()
                                        << " acks for frame: " << frame_number << std::endl;
                for (const auto& value : values_to_ack_it->second)
                {
                    goby::glog.is_debug1() && goby::glog << group(glog_group_)
                                                         << "Publishing ack for "
                                                         << value.subbuffer_id << std::endl;

                    _ack_value(ack_pair, now, value);
                    buffer_.erase(value);
                }
                pending_ack_.erase(values_to_ack_it);
                // TODO publish acks for other drivers to erase the same piece of data (if they have it and the ack'ing party is the same vehicle - need a distinction between modem_id and vehicle_id ?)
            }
        }
    }
    else
    {
        if (full_dest == _broadcast_id() || full_dest == this->cfg().driver().modem_id())
        {
            for (auto& frame : rx_msg.frame())
            {
                try
                {
                    intervehicle::protobuf::DCCLForwardedData packets(
                        detail::DCCLSerializerParserHelperBase::unpack(frame));
                    packets.mutable_header()->set_src(full_src);
                    packets.mutable_header()->add_dest(full_dest);
                    *packets.mutable_header()->mutable_modem_msg() = rx_msg;
                    interprocess_->template publish<groups::modem_data_in>(packets);
                }
                catch (const std::exception& e)
                {
                    goby::glog.is_warn() && goby::glog << group(glog_group_)
                                                       << "Failed to unpack frame ["
                                                       << goby::util::hex_encode(frame)
                                                       << "]: " << e.what() << std::endl;
                }
            }
        }
    }
}

template <typename ImplementationTag>
void goby::middleware::intervehicle::ModemDriverThread<ImplementationTag>::
    _publish_subscription_report(const intervehicle::protobuf::Subscription& changed)
{
    protobuf::SubscriptionReport report;
    report.set_link_modem_id(this->cfg().modem_id());
    for (const auto& sub_id_p : subscriber_buffer_cfg_)
    {
        for (const auto& subbuffer_sub_p : sub_id_p.second)
            *report.add_subscription() = subbuffer_sub_p.second;
    }
    *report.mutable_changed() = changed;
    interprocess_->template publish<groups::subscription_report>(report);
}

#include "goby/udpm/transport/detail/tags.h"
#include "goby/zenoh/transport/detail/tags.h"
#include "goby/zeromq/transport/detail/tags.h"

// Explicit template instantiations for known implementation tags
namespace goby::middleware::intervehicle
{
template class ModemDriverThread<::goby::zeromq::detail::InterProcessTag>;
template class ModemDriverThread<::goby::udpm::detail::InterProcessTag>;
template class ModemDriverThread<::goby::zenoh::detail::InterProcessTag>;
} // namespace goby::middleware::intervehicle
