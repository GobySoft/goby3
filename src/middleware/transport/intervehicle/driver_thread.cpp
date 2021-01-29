// Copyright 2017-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include "goby/acomms/acomms_constants.h"                   // for BROADCAS...
#include "goby/acomms/bind.h"                               // for bind
#include "goby/acomms/modemdriver/benthos_atm900_driver.h"  // for BenthosA...
#include "goby/acomms/modemdriver/iridium_driver.h"         // for IridiumD...
#include "goby/acomms/modemdriver/iridium_shore_driver.h"   // for IridiumS...
#include "goby/acomms/modemdriver/mm_driver.h"              // for MMDriver
#include "goby/acomms/modemdriver/popoto_driver.h"          // for PopotoDr...
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

#include "driver_thread.h"

using goby::glog;
using namespace goby::util::logger;
using goby::middleware::protobuf::SerializerTransporterMessage;

goby::middleware::intervehicle::ModemDriverThread::ModemDriverThread(
    const intervehicle::protobuf::PortalConfig::LinkConfig& config)
    : goby::middleware::Thread<intervehicle::protobuf::PortalConfig::LinkConfig,
                               InterProcessForwarder<InterThreadTransporter>>(
          config, 10 * boost::units::si::hertz),
      glog_group_("goby::middleware::intervehicle::driver_thread::id" +
                  std::to_string(cfg().driver().modem_id()))

{
    goby::glog.add_group(glog_group_, util::Colors::blue);
    interthread_ = std::make_unique<InterThreadTransporter>();
    interprocess_ = std::make_unique<InterProcessForwarder<InterThreadTransporter>>(*interthread_);
    this->set_transporter(interprocess_.get());

    interprocess_->subscribe<groups::modem_data_out, SerializerTransporterMessage>(
        [this](std::shared_ptr<const SerializerTransporterMessage> msg) {
            _buffer_message(std::move(msg));
        });

    interprocess_->subscribe<groups::modem_subscription_forward_tx,
                             intervehicle::protobuf::Subscription, MarshallingScheme::PROTOBUF>(
        [this](const std::shared_ptr<const intervehicle::protobuf::Subscription>& subscription) {
            _forward_subscription(*subscription);
        });

    interprocess_->subscribe<groups::modem_subscription_forward_rx,
                             intervehicle::protobuf::Subscription, MarshallingScheme::PROTOBUF>(
        [this](const std::shared_ptr<const intervehicle::protobuf::Subscription>& subscription) {
            _accept_subscription(*subscription);
        });

    if (cfg().driver().has_driver_name())
    {
        throw(goby::Exception("Driver plugins not yet supported by InterVehicle transporters: use "
                              "driver_type enumerations."));
    }
    else
    {
        switch (cfg().driver().driver_type())
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

            case goby::acomms::protobuf::DRIVER_POPOTO:
                driver_ = std::make_unique<goby::acomms::PopotoDriver>();
                break;

            case goby::acomms::protobuf::DRIVER_NONE:
            case goby::acomms::protobuf::DRIVER_ABC_EXAMPLE_MODEM:
            case goby::acomms::protobuf::DRIVER_UFIELD_SIM_DRIVER:
            case goby::acomms::protobuf::DRIVER_BLUEFIN_MOOS:
                throw(goby::Exception(
                    "Unsupported driver type: " +
                    goby::acomms::protobuf::DriverType_Name(cfg().driver().driver_type())));
                break;
        }
    }

    driver_->signal_receive.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& rx_msg) { _receive(rx_msg); });

    driver_->signal_data_request.connect(
        [&](goby::acomms::protobuf::ModemTransmission* msg) { this->_data_request(msg); });

    goby::acomms::bind(mac_, *driver_);

    mac_.startup(cfg().mac());
    driver_->startup(cfg().driver());

    subscription_key_.set_marshalling_scheme(MarshallingScheme::DCCL);
    subscription_key_.set_type(intervehicle::protobuf::Subscription::descriptor()->full_name());
    subscription_key_.set_group_numeric(Group::broadcast_group);

    goby::glog.is_debug1() && goby::glog << group(glog_group_) << "Driver ready" << std::endl;
    interthread_->publish<groups::modem_driver_ready, bool>(true);
}

void goby::middleware::intervehicle::ModemDriverThread::loop()
{
    auto expired = buffer_.expire();

    if (!expired.empty())
    {
        auto now = goby::time::SteadyClock::now();
        for (const auto& value : expired)
            _expire_value(now, value,
                          intervehicle::protobuf::ExpireData::EXPIRED_TIME_TO_LIVE_EXCEEDED);
    }

    driver_->do_work();
    mac_.do_work();
}

void goby::middleware::intervehicle::ModemDriverThread::_expire_value(
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
    interprocess_->publish<groups::modem_expire_in>(expire_pair);
}

void goby::middleware::intervehicle::ModemDriverThread::_forward_subscription(
    intervehicle::protobuf::Subscription subscription)
{
    if (subscription.has_metadata())
        detail::DCCLSerializerParserHelperBase::load_metadata(subscription.metadata());

    subscription.mutable_header()->set_src(cfg().driver().modem_id());
    for (auto dest : subscription.header().dest())
    {
        if (!_dest_is_in_subnet(dest))
            continue;

        auto buffer_id = _create_buffer_id(subscription_key_);
        if (!subscription_subbuffers_.count(dest))
        {
            auto subscription_buffer_cfg = cfg().subscription_buffer();
            if (!subscription_buffer_cfg.has_ack_required())
                subscription_buffer_cfg.set_ack_required(true);

            using value_base_type =
                std::result_of<decltype (&goby::acomms::protobuf::DynamicBufferConfig::value_base)(
                    goby::acomms::protobuf::DynamicBufferConfig)>::type;

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

        buffer_.push({dest, buffer_id, goby::time::SteadyClock::now(), *subscription_publication});
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_data_request(
    goby::acomms::protobuf::ModemTransmission* msg)
{
    // erase any pending acks with greater frame numbers (we never received these)
    auto it = pending_ack_.lower_bound(msg->frame_start()), end = pending_ack_.end();
    while (it != end)
    {
        goby::glog << group(glog_group_) << "Erasing " << it->second.size()
                   << " values not acked for frame " << it->first << std::endl;
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
                                    cfg().ack_timeout_with_units()));
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
            catch (goby::acomms::DynamicBufferNoDataException&)
            {
                break;
            }
        }
    }

    if (!msg->has_ack_requested())
        msg->set_ack_requested(false);

    msg->set_dest(dest);
}

goby::middleware::intervehicle::ModemDriverThread::subbuffer_id_type
goby::middleware::intervehicle::ModemDriverThread::_create_buffer_id(unsigned dccl_id,
                                                                     unsigned group)
{
    return "/group:" + std::to_string(group) + "/id:" + std::to_string(dccl_id) + "/";
}

void goby::middleware::intervehicle::ModemDriverThread::_accept_subscription(
    const intervehicle::protobuf::Subscription& subscription)
{
    auto buffer_id = _create_buffer_id(subscription);

    glog.is_debug2() &&
        glog << group(glog_group_) << "Received new forwarded subscription/unsubscription: "
             << subscription.ShortDebugString() << ", buffer_id: " << buffer_id << std::endl;

    auto dest = subscription.header().src();

    if (!_dest_is_in_subnet(dest))
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
                    is_new_cfg = false;
                break;
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

void goby::middleware::intervehicle::ModemDriverThread::_try_create_or_update_buffer(
    modem_id_type dest_id, const subbuffer_id_type& buffer_id)
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

void goby::middleware::intervehicle::ModemDriverThread::_buffer_message(
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
        interprocess_->publish<groups::metadata_request>(meta_request);
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
        interprocess_->publish<groups::metadata_request>(meta_request);
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
        _expire_value(now, {cfg().driver().modem_id(), buffer_id, now, *msg},
                      intervehicle::protobuf::ExpireData::EXPIRED_NO_SUBSCRIBERS);
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_receive(
    const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    glog.is(DEBUG1) && glog << group(glog_group_) << "Received: " << rx_msg.ShortDebugString()
                            << std::endl;
    if (rx_msg.type() == goby::acomms::protobuf::ModemTransmission::ACK)
    {
        if (rx_msg.dest() != cfg().driver().modem_id())
        {
            glog.is(WARN) && glog << group(glog_group_)
                                  << "ignoring ack for modem_id = " << rx_msg.dest() << std::endl;
            return;
        }
        for (int i = 0, n = rx_msg.acked_frame_size(); i < n; ++i)
        {
            int frame_number = rx_msg.acked_frame(i);
            if (!pending_ack_.count(frame_number))
            {
                glog.is(DEBUG1) &&
                    glog << group(glog_group_) << "got ack but we were not expecting one from "
                         << rx_msg.src() << " for frame " << frame_number << std::endl;
                continue;
            }
            else
            {
                protobuf::AckMessagePair ack_pair;
                protobuf::AckData& ack_data = *ack_pair.mutable_data();
                ack_data.mutable_header()->set_src(rx_msg.src());
                ack_data.mutable_header()->add_dest(rx_msg.dest());
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

                    ack_data.set_latency_with_units(
                        goby::time::convert_duration<goby::time::MicroTime>(now - value.push_time));

                    *ack_pair.mutable_serializer() = value.data;
                    interprocess_->publish<groups::modem_ack_in>(ack_pair);
                    buffer_.erase(value);
                }
                pending_ack_.erase(values_to_ack_it);
                // TODO publish acks for other drivers to erase the same piece of data (if they have it and the ack'ing party is the same vehicle - need a distinction between modem_id and vehicle_id ?)
            }
        }
    }
    else
    {
        if (rx_msg.dest() == goby::acomms::BROADCAST_ID ||
            rx_msg.dest() == cfg().driver().modem_id())
        {
            for (auto& frame : rx_msg.frame())
            {
                const intervehicle::protobuf::DCCLForwardedData packets(
                    detail::DCCLSerializerParserHelperBase::unpack(frame));
                interprocess_->publish<groups::modem_data_in>(packets);
            }
        }
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_publish_subscription_report(
    const intervehicle::protobuf::Subscription& changed)
{
    protobuf::SubscriptionReport report;
    report.set_link_modem_id(cfg().modem_id());
    for (const auto& sub_id_p : subscriber_buffer_cfg_)
    {
        for (const auto& subbuffer_sub_p : sub_id_p.second)
            *report.add_subscription() = subbuffer_sub_p.second;
    }
    *report.mutable_changed() = changed;
    interprocess_->publish<groups::subscription_report>(report);
}
