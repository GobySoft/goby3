// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <cctype>
#include <dlfcn.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/math/special_functions/fpclassify.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "goby/acomms/modemdriver/benthos_atm900_driver.h"
#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include "goby/moos/moos_bluefin_driver.h"
#include "goby/moos/moos_protobuf_helpers.h"
#include "goby/moos/moos_ufield_sim_driver.h"
#include "goby/moos/protobuf/bluefin_driver.pb.h"
#include "goby/moos/protobuf/frontseat.pb.h"
#include "goby/moos/protobuf/ufield_sim_driver.pb.h"
#include "goby/time/io.h"
#include "goby/util/protobuf/io.h"
#include "goby/util/sci.h"

#include "pAcommsHandler.h"

using namespace goby::util::tcolor;
using namespace goby::util::logger;
using goby::acomms::operator<<;
using goby::moos::operator<<;
using goby::util::as;
using google::protobuf::uint32;

using goby::glog;

using goby::apps::moos::protobuf::pAcommsHandlerConfig;

pAcommsHandlerConfig goby::apps::moos::CpAcommsHandler::cfg_;
goby::apps::moos::CpAcommsHandler* goby::apps::moos::CpAcommsHandler::inst_ = 0;
std::map<std::string, void*> goby::apps::moos::CpAcommsHandler::driver_plugins_;

goby::apps::moos::CpAcommsHandler* goby::apps::moos::CpAcommsHandler::get_instance()
{
    if (!inst_)
        inst_ = new goby::apps::moos::CpAcommsHandler();
    return inst_;
}

void goby::apps::moos::CpAcommsHandler::delete_instance() { delete inst_; }

goby::apps::moos::CpAcommsHandler::CpAcommsHandler()
    : goby::moos::GobyMOOSApp(&cfg_),
      translator_(goby::moos::protobuf::TranslatorEntry(), cfg_.common().lat_origin(),
                  cfg_.common().lon_origin(), cfg_.modem_id_lookup_path()),
      dccl_(goby::acomms::DCCLCodec::get()),
      work_(timer_io_service_),
      router_(0)
{
    translator_.add_entry(cfg_.translator_entry());

    goby::acomms::connect(&queue_manager_.signal_receive, this,
                          &CpAcommsHandler::handle_queue_receive);

    // informational 'queue' signals
    goby::acomms::connect(&queue_manager_.signal_ack,
                          boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                      cfg_.moos_var().queue_ack_transmission(), _2,
                                      cfg_.moos_var().queue_ack_original_msg()));
    goby::acomms::connect(&queue_manager_.signal_receive,
                          boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                      cfg_.moos_var().queue_receive(), _1, ""));
    goby::acomms::connect(&queue_manager_.signal_expire,
                          boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                      cfg_.moos_var().queue_expire(), _1, ""));
    goby::acomms::connect(&queue_manager_.signal_queue_size_change,
                          boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                      cfg_.moos_var().queue_size(), _1, ""));

    // informational 'mac' signals
    goby::acomms::connect(&mac_.signal_initiate_transmission,
                          boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                      cfg_.moos_var().mac_initiate_transmission(), _1, ""));

    goby::acomms::connect(&mac_.signal_slot_start,
                          boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                      cfg_.moos_var().mac_slot_start(), _1, ""));

    goby::acomms::connect(&queue_manager_.signal_data_on_demand, this,
                          &CpAcommsHandler::handle_encode_on_demand);

    process_configuration();

    driver_bind();

    for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                  goby::acomms::protobuf::DriverConfig*>::iterator it = drivers_.begin(),
                                                                   end = drivers_.end();
         it != end; ++it)
        goby::acomms::bind(*(it->first), queue_manager_);

    if (router_)
    {
        bind(queue_manager_, *router_);
    }

    // update comms cycle
    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().mac_cycle_update(),
              &CpAcommsHandler::handle_mac_cycle_update, this);

    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().queue_flush(),
              &CpAcommsHandler::handle_flush_queue, this);

    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().config_file_request(),
              &CpAcommsHandler::handle_config_file_request, this);

    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().mac_initiate_transmission(),
              &CpAcommsHandler::handle_external_initiate_transmission, this);

    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().driver_reset(),
              &CpAcommsHandler::handle_driver_reset, this);

    subscribe_pb(cfg_.moos_var().prefix() + cfg_.moos_var().driver_cfg_update(),
                 &CpAcommsHandler::handle_driver_cfg_update, this);
}

goby::apps::moos::CpAcommsHandler::~CpAcommsHandler() {}

void goby::apps::moos::CpAcommsHandler::loop()
{
    timer_io_service_.poll();

    if (driver_restart_time_.size())
        restart_drivers();

    for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                  goby::acomms::protobuf::DriverConfig*>::iterator it = drivers_.begin(),
                                                                   end = drivers_.end();
         it != end; ++it)
    {
        if (!driver_restart_time_.count(it->first))
        {
            try
            {
                it->first->do_work();
            }
            catch (goby::acomms::ModemDriverException& e)
            {
                driver_reset(it->first, e);
                break; // no longer valid drivers_ container
            }
        }
    }

    // don't run the MAC if the primary driver is shutdown
    if (!driver_restart_time_.count(driver_))
        mac_.do_work();
    queue_manager_.do_work();
}

//
// Mail Handlers
//

void goby::apps::moos::CpAcommsHandler::handle_mac_cycle_update(const CMOOSMsg& msg)
{
    goby::acomms::protobuf::MACUpdate update_msg;
    parse_for_moos(msg.GetString(), &update_msg);

    glog << group("pAcommsHandler") << "got update for MAC: " << update_msg << std::endl;

    if (update_msg.dest() != cfg_.modem_id())
    {
        glog << group("pAcommsHandler") << "update not for us" << std::endl;
        return;
    }

    goby::acomms::MACManager::iterator it1 = mac_.begin(), it2 = mac_.begin();

    for (int i = 0, n = update_msg.first_iterator(); i < n; ++i) ++it1;

    for (int i = 0, n = update_msg.second_iterator(); i < n; ++i) ++it2;

    switch (update_msg.update_type())
    {
        case goby::acomms::protobuf::MACUpdate::ASSIGN:
            mac_.assign(update_msg.slot().begin(), update_msg.slot().end());
            break;

        case goby::acomms::protobuf::MACUpdate::PUSH_BACK:
            for (int i = 0, n = update_msg.slot_size(); i < n; ++i)
                mac_.push_back(update_msg.slot(i));
            break;

        case goby::acomms::protobuf::MACUpdate::PUSH_FRONT:
            for (int i = 0, n = update_msg.slot_size(); i < n; ++i)
                mac_.push_front(update_msg.slot(i));
            break;

        case goby::acomms::protobuf::MACUpdate::POP_BACK:
            if (mac_.size())
                mac_.pop_back();
            else
                glog.is(WARN) && glog << group("pAcommsHandler")
                                      << "Cannot POP_BACK of empty MAC cycle" << std::endl;
            break;

        case goby::acomms::protobuf::MACUpdate::POP_FRONT:
            if (mac_.size())
                mac_.pop_front();
            else
                glog.is(WARN) && glog << group("pAcommsHandler")
                                      << "Cannot POP_FRONT of empty MAC cycle" << std::endl;
            break;

        case goby::acomms::protobuf::MACUpdate::INSERT:
            mac_.insert(it1, update_msg.slot().begin(), update_msg.slot().end());
            break;

        case goby::acomms::protobuf::MACUpdate::ERASE:
            if (update_msg.second_iterator() != -1)
                mac_.erase(it1, it2);
            else
                mac_.erase(it1);
            break;

        case goby::acomms::protobuf::MACUpdate::CLEAR: mac_.clear(); break;

        case goby::acomms::protobuf::MACUpdate::NO_CHANGE: break;
    }

    mac_.update();

    if (update_msg.has_cycle_state())
    {
        switch (update_msg.cycle_state())
        {
            case goby::acomms::protobuf::MACUpdate::STARTED:
                mac_.restart();

                for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                              goby::acomms::protobuf::DriverConfig*>::iterator
                         it = drivers_.begin(),
                         end = drivers_.end();
                     it != end; ++it)
                {
                    if (!driver_restart_time_.count(it->first))
                    {
                        goby::acomms::MMDriver* driver =
                            dynamic_cast<goby::acomms::MMDriver*>(it->first.get());
                        if (driver)
                            driver->set_silent(false);
                    }
                }
                break;

            case goby::acomms::protobuf::MACUpdate::STOPPED:
                for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                              goby::acomms::protobuf::DriverConfig*>::iterator
                         it = drivers_.begin(),
                         end = drivers_.end();
                     it != end; ++it)
                {
                    if (!driver_restart_time_.count(it->first))
                    {
                        goby::acomms::MMDriver* driver =
                            dynamic_cast<goby::acomms::MMDriver*>(it->first.get());
                        if (driver)
                            driver->set_silent(true);
                    }
                }

                mac_.shutdown();
                break;
        }
    }
}

void goby::apps::moos::CpAcommsHandler::handle_flush_queue(const CMOOSMsg& msg)
{
    goby::acomms::protobuf::QueueFlush flush;
    parse_for_moos(msg.GetString(), &flush);

    glog.is(VERBOSE) && glog << group("pAcommsHandler") << "Queue flush request: " << flush
                             << std::endl;
    queue_manager_.flush_queue(flush);
}

void goby::apps::moos::CpAcommsHandler::handle_config_file_request(const CMOOSMsg&)
{
    publish(cfg_.moos_var().prefix() + cfg_.moos_var().config_file(),
            dccl::b64_encode(cfg_.SerializeAsString()));
}

void goby::apps::moos::CpAcommsHandler::handle_driver_reset(const CMOOSMsg& msg)
{
    driver_reset(driver_,
                 goby::acomms::ModemDriverException(
                     "Manual reset", goby::acomms::protobuf::ModemDriverStatus::MANUAL_RESET));
}

void goby::apps::moos::CpAcommsHandler::handle_driver_cfg_update(
    const goby::acomms::protobuf::DriverConfig& cfg)
{
    glog.is(VERBOSE) && glog << group("pAcommsHandler") << "Driver config update request: " << cfg
                             << std::endl;

    bool driver_found = false;
    for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                  goby::acomms::protobuf::DriverConfig*>::iterator it = drivers_.begin(),
                                                                   end = drivers_.end();
         it != end; ++it)
    {
        if (it->second->modem_id() == cfg.modem_id())
        {
            driver_found = true;
            if (it->first && !driver_restart_time_.count(it->first))
                it->first->update_cfg(cfg);
        }
    }
    if (!driver_found)
        glog.is(WARN) && glog << group("pAcommsHandler")
                              << "Could not find driver with modem id: " << cfg.modem_id()
                              << " to update";
}

void goby::apps::moos::CpAcommsHandler::handle_external_initiate_transmission(const CMOOSMsg& msg)
{
    // don't repost our own transmissions
    if (msg.GetSource() == CMOOSApp::GetAppName())
        return;

    if (driver_)
    {
        goby::acomms::protobuf::ModemTransmission transmission;
        parse_for_moos(msg.GetString(), &transmission);

        glog.is(VERBOSE) && glog << group("pAcommsHandler")
                                 << "Initiating transmission: " << transmission << std::endl;
        driver_->handle_initiate_transmission(transmission);
    }
}

void goby::apps::moos::CpAcommsHandler::handle_goby_signal(const google::protobuf::Message& msg1,
                                                           const std::string& moos_var1,
                                                           const google::protobuf::Message& msg2,
                                                           const std::string& moos_var2)

{
    if (!moos_var1.empty())
        publish_pb(cfg_.moos_var().prefix() + moos_var1, msg1);

    if (!moos_var2.empty())
        publish_pb(cfg_.moos_var().prefix() + moos_var2, msg2);
}

void goby::apps::moos::CpAcommsHandler::handle_raw(const goby::acomms::protobuf::ModemRaw& msg,
                                                   const std::string& moos_var)
{
    publish(cfg_.moos_var().prefix() + moos_var, msg.raw());
}

//
// READ CONFIGURATION
//

void goby::apps::moos::CpAcommsHandler::process_configuration()
{
    // create driver objects
    create_driver(driver_, cfg_.mutable_driver_cfg(), &mac_);
    if (driver_)
        drivers_.insert(std::make_pair(driver_, cfg_.mutable_driver_cfg()));

    // create receive only (listener) drivers
    for (int i = 0, n = cfg_.listen_driver_cfg_size(); i < n; ++i)
    {
        std::shared_ptr<goby::acomms::ModemDriverBase> driver;
        create_driver(driver, cfg_.mutable_listen_driver_cfg(i), 0);
        drivers_.insert(std::make_pair(driver, cfg_.mutable_listen_driver_cfg(i)));
    }

    if (cfg_.has_route_cfg() && cfg_.route_cfg().route().hop_size() > 0)
    {
        router_ = new goby::acomms::RouteManager;
    }

    // check and propagate modem id
    if (cfg_.modem_id() == goby::acomms::BROADCAST_ID)
        glog.is(DIE) &&
            glog << "modem_id = " << goby::acomms::BROADCAST_ID
                 << " is reserved for broadcast messages. You must specify a modem_id != "
                 << goby::acomms::BROADCAST_ID << " for this vehicle." << std::endl;

    publish("MODEM_ID", cfg_.modem_id());
    publish("VEHICLE_ID", cfg_.modem_id());

    cfg_.mutable_queue_cfg()->set_modem_id(cfg_.modem_id());
    cfg_.mutable_mac_cfg()->set_modem_id(cfg_.modem_id());

    for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                  goby::acomms::protobuf::DriverConfig*>::iterator it = drivers_.begin(),
                                                                   end = drivers_.end();
         it != end; ++it)
    {
        if (!it->second->has_modem_id())
            it->second->set_modem_id(cfg_.modem_id());
    }

    std::vector<void*> handles;
    // load all shared libraries
    for (int i = 0, n = cfg_.load_shared_library_size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << group("pAcommsHandler")
                                 << "Loading shared library: " << cfg_.load_shared_library(i)
                                 << std::endl;

        void* handle =
            dccl::DynamicProtobufManager::load_from_shared_lib(cfg_.load_shared_library(i));
        handles.push_back(handle);

        if (!handle)
        {
            glog.is(DIE) && glog << "Failed ... check path provided or add to /etc/ld.so.conf "
                                 << "or LD_LIBRARY_PATH" << std::endl;
        }

        glog << group("pAcommsHandler") << "Loading shared library dccl codecs." << std::endl;
    }

    // set id codec before shared library load
    dccl_->set_cfg(cfg_.dccl_cfg());
    for (int i = 0, n = handles.size(); i < n; ++i) dccl_->load_shared_library_codecs(handles[i]);

    // load all .proto files
    dccl::DynamicProtobufManager::enable_compilation();
    for (int i = 0, n = cfg_.load_proto_file_size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << group("pAcommsHandler")
                                 << "Loading protobuf file: " << cfg_.load_proto_file(i)
                                 << std::endl;

        if (!dccl::DynamicProtobufManager::load_from_proto_file(cfg_.load_proto_file(i)))
            glog.is(DIE) && glog << "Failed to load file." << std::endl;
    }

    // start goby-acomms classes

    for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                  goby::acomms::protobuf::DriverConfig*>::iterator it = drivers_.begin(),
                                                                   end = drivers_.end();
         it != end; ++it)
        driver_restart_time_.insert(std::make_pair(it->first, 0));

    mac_.startup(cfg_.mac_cfg());
    queue_manager_.set_cfg(cfg_.queue_cfg());
    if (router_)
        router_->set_cfg(cfg_.route_cfg());

    // process translator entries
    for (int i = 0, n = cfg_.translator_entry_size(); i < n; ++i)
    {
        typedef std::shared_ptr<google::protobuf::Message> GoogleProtobufMessagePointer;
        glog.is(VERBOSE) && glog << group("pAcommsHandler") << "Checking translator entry: "
                                 << cfg_.translator_entry(i).protobuf_name() << std::endl;

        // check that the protobuf file is loaded somehow
        GoogleProtobufMessagePointer msg =
            dccl::DynamicProtobufManager::new_protobuf_message<GoogleProtobufMessagePointer>(
                cfg_.translator_entry(i).protobuf_name());

        if (cfg_.translator_entry(i).trigger().type() ==
            goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_PUBLISH)
        {
            // subscribe for trigger publish variables
            GobyMOOSApp::subscribe(cfg_.translator_entry(i).trigger().moos_var(),
                                   boost::bind(&CpAcommsHandler::create_on_publish, this, _1,
                                               cfg_.translator_entry(i)));
        }
        else if (cfg_.translator_entry(i).trigger().type() ==
                 goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_TIME)
        {
            timers_.push_back(std::shared_ptr<Timer>(new Timer(timer_io_service_)));

            Timer& new_timer = *timers_.back();

            new_timer.expires_from_now(
                std::chrono::seconds(cfg_.translator_entry(i).trigger().period()));
            // Start an asynchronous wait.
            new_timer.async_wait(boost::bind(&CpAcommsHandler::create_on_timer, this, _1,
                                             cfg_.translator_entry(i), &new_timer));
        }

        for (int j = 0, m = cfg_.translator_entry(i).create_size(); j < m; ++j)
        {
            // subscribe for all create variables
            subscribe(cfg_.translator_entry(i).create(j).moos_var());
        }
    }

    for (int i = 0, m = cfg_.multiplex_create_moos_var_size(); i < m; ++i)
    {
        GobyMOOSApp::subscribe(cfg_.multiplex_create_moos_var(i),
                               &CpAcommsHandler::create_on_multiplex_publish, this);
    }

    for (int i = 0, n = cfg_.dccl_frontseat_forward_name_size(); i < n; ++i)
    {
        const google::protobuf::Descriptor* desc =
            dccl::DynamicProtobufManager::find_descriptor(cfg_.dccl_frontseat_forward_name(i));
        if (desc)
        {
            dccl_frontseat_forward_.insert(desc);
        }
        else
        {
            glog.is(DIE) && glog << "Invalid message name given to dccl_frontseat_forward_name: "
                                 << cfg_.dccl_frontseat_forward_name(i) << std::endl;
        }
    }
}

void goby::apps::moos::CpAcommsHandler::create_driver(
    std::shared_ptr<goby::acomms::ModemDriverBase>& driver,
    goby::acomms::protobuf::DriverConfig* driver_cfg, goby::acomms::MACManager* mac)
{
    if (driver_cfg->has_driver_name())
    {
        std::map<std::string, void*>::const_iterator driver_it =
            driver_plugins_.find(driver_cfg->driver_name());

        if (driver_it == driver_plugins_.end())
            glog.is(DIE) && glog << "Could not find driver_plugin_name '"
                                 << driver_cfg->driver_name()
                                 << "'. Make sure it is loaded using the PACOMMSHANDLER_PLUGINS "
                                    "environmental var"
                                 << std::endl;
        else
        {
            goby::acomms::ModemDriverBase* (*driver_function)(void) =
                (goby::acomms::ModemDriverBase * (*)(void))
                    dlsym(driver_it->second, "goby_make_driver");

            if (!driver_function)
            {
                glog.is(DIE) && glog << "Could not load goby::acomms::ModemDriverBase* "
                                        "goby_make_driver() for driver name '"
                                     << driver_cfg->driver_name() << "'." << std::endl;
            }
            else
            {
                driver.reset((*driver_function)());
            }
        }
    }
    else
    {
        switch (driver_cfg->driver_type())
        {
            case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
                driver.reset(new goby::acomms::MMDriver);
                break;

            case goby::acomms::protobuf::DRIVER_BENTHOS_ATM900:
                driver.reset(new goby::acomms::BenthosATM900Driver);
                break;

            case goby::acomms::protobuf::DRIVER_ABC_EXAMPLE_MODEM:
                driver.reset(new goby::acomms::ABCDriver);
                break;

            case goby::acomms::protobuf::DRIVER_UFIELD_SIM_DRIVER:
                driver.reset(new goby::moos::UFldDriver);
                driver_cfg->MutableExtension(goby::moos::ufld::protobuf::config)
                    ->set_modem_id_lookup_path(cfg_.modem_id_lookup_path());
                break;

            case goby::acomms::protobuf::DRIVER_IRIDIUM:
                driver.reset(new goby::acomms::IridiumDriver);
                break;

            case goby::acomms::protobuf::DRIVER_UDP:
                driver.reset(new goby::acomms::UDPDriver);
                break;

            case goby::acomms::protobuf::DRIVER_UDP_MULTICAST:
                driver.reset(new goby::acomms::UDPMulticastDriver);
                break;

            case goby::acomms::protobuf::DRIVER_BLUEFIN_MOOS:
                driver.reset(new goby::moos::BluefinCommsDriver(mac));
                driver_cfg->MutableExtension(goby::moos::bluefin::protobuf::config)
                    ->set_moos_server(cfg_.common().server_host());
                driver_cfg->MutableExtension(goby::moos::bluefin::protobuf::config)
                    ->set_moos_port(cfg_.common().server_port());
                break;

            case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
                driver.reset(new goby::acomms::IridiumShoreDriver);
                break;

            case goby::acomms::protobuf::DRIVER_NONE: break;
        }
    }
}

void goby::apps::moos::CpAcommsHandler::handle_queue_receive(const google::protobuf::Message& msg)
{
    try
    {
        std::multimap<std::string, CMOOSMsg> out;

        out = translator_.protobuf_to_moos(msg);

        for (std::multimap<std::string, CMOOSMsg>::iterator it = out.begin(), n = out.end();
             it != n; ++it)
        {
            glog.is(DEBUG2) && glog << group("pAcommsHandler") << "Publishing: " << it->second
                                    << std::endl;
            publish(it->second);
        }
    }
    catch (std::runtime_error& e)
    {
        glog.is(WARN) && glog << group("pAcommsHandler")
                              << "Failed to translate received message: " << e.what() << std::endl;
    }

    // forward to frontseat driver
    if (dccl_frontseat_forward_.count(msg.GetDescriptor()))
    {
        goby::moos::protobuf::FrontSeatInterfaceData fs_data;
        dccl_->encode(fs_data.mutable_dccl_message(), msg);
        publish_pb(cfg_.moos_var().ifrontseat_data_out(), fs_data);
    }

    // handle various commands

    if (router_ && msg.GetDescriptor() == goby::acomms::protobuf::RouteCommand::descriptor())
    {
        goby::acomms::protobuf::RouteCommand route_cmd;
        route_cmd.CopyFrom(msg);
        glog.is(VERBOSE) && glog << group("pAcommsHandler")
                                 << "Received RouteCommand: " << msg.DebugString() << std::endl;
        goby::acomms::protobuf::RouteManagerConfig cfg = cfg_.route_cfg();
        cfg.mutable_route()->CopyFrom(route_cmd.new_route());
        router_->set_cfg(cfg);
    }
}

void goby::apps::moos::CpAcommsHandler::handle_encode_on_demand(
    const goby::acomms::protobuf::ModemTransmission& request_msg,
    google::protobuf::Message* data_msg)
{
    glog.is(VERBOSE) && glog << group("pAcommsHandler")
                             << "Received encode on demand request: " << request_msg << std::endl;

    std::shared_ptr<google::protobuf::Message> created_message =
        translator_.moos_to_protobuf<std::shared_ptr<google::protobuf::Message>>(
            dynamic_vars().all(), data_msg->GetDescriptor()->full_name());

    data_msg->CopyFrom(*created_message);
}

void goby::apps::moos::CpAcommsHandler::create_on_publish(
    const CMOOSMsg& trigger_msg, const goby::moos::protobuf::TranslatorEntry& entry)
{
    glog.is(DEBUG2) && glog << group("pAcommsHandler")
                            << "Received trigger: " << trigger_msg.GetKey() << std::endl;

    if (!entry.trigger().has_mandatory_content() ||
        trigger_msg.GetString().find(entry.trigger().mandatory_content()) != std::string::npos)
        translate_and_push(entry);
    else
        glog.is(DEBUG2) &&
            glog << group("pAcommsHandler")
                 << "Message missing mandatory content for: " << entry.protobuf_name() << std::endl;
}

void goby::apps::moos::CpAcommsHandler::create_on_multiplex_publish(const CMOOSMsg& moos_msg)
{
    std::shared_ptr<google::protobuf::Message> msg = dynamic_parse_for_moos(moos_msg.GetString());

    if (!msg)
    {
        glog.is(WARN) && glog << group("pAcommsHandler")
                              << "Multiplex receive failed: Unknown Protobuf type for "
                              << moos_msg.GetString()
                              << "; be sure it is compiled in or directly loaded into the "
                                 "dccl::DynamicProtobufManager."
                              << std::endl;
        return;
    }

    std::multimap<std::string, CMOOSMsg> out;

    try
    {
        out = translator_.protobuf_to_inverse_moos(*msg);

        for (std::multimap<std::string, CMOOSMsg>::iterator it = out.begin(), n = out.end();
             it != n; ++it)
        {
            glog.is(VERBOSE) && glog << group("pAcommsHandler")
                                     << "Inverse Publishing: " << it->second.GetKey() << std::endl;
            publish(it->second);
        }
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << group("pAcommsHandler")
                              << "Failed to inverse publish: " << e.what() << std::endl;
    }
}

void goby::apps::moos::CpAcommsHandler::create_on_timer(
    const boost::system::error_code& error, const goby::moos::protobuf::TranslatorEntry& entry,
    Timer* timer)
{
    if (!error)
    {
        double skew_seconds = std::abs((goby::time::SystemClock::now() - timer->expires_at()) /
                                       std::chrono::seconds(1));
        if (skew_seconds > ALLOWED_TIMER_SKEW_SECONDS)
        {
            glog.is(VERBOSE) && glog << group("pAcommsHandler") << warn << "clock skew of "
                                     << skew_seconds << " seconds detected, resetting timer."
                                     << std::endl;
            timer->expires_at(goby::time::SystemClock::now() +
                              std::chrono::seconds(entry.trigger().period()));
        }
        else
        {
            // reset the timer
            timer->expires_at(timer->expires_at() + std::chrono::seconds(entry.trigger().period()));
        }

        timer->async_wait(boost::bind(&CpAcommsHandler::create_on_timer, this, _1, entry, timer));

        glog.is(DEBUG2) && glog << group("pAcommsHandler")
                                << "Received trigger for: " << entry.protobuf_name() << std::endl;
        glog.is(DEBUG2) && glog << group("pAcommsHandler") << "Next expiry: " << timer->expires_at()
                                << std::endl;

        translate_and_push(entry);
    }
}

void goby::apps::moos::CpAcommsHandler::translate_and_push(
    const goby::moos::protobuf::TranslatorEntry& entry)
{
    try
    {
        std::shared_ptr<google::protobuf::Message> created_message =
            translator_.moos_to_protobuf<std::shared_ptr<google::protobuf::Message>>(
                dynamic_vars().all(), entry.protobuf_name());

        glog.is(DEBUG2) && glog << group("pAcommsHandler") << "Created message: \n"
                                << created_message->DebugString() << std::endl;

        queue_manager_.push_message(*created_message);
    }
    catch (std::runtime_error& e)
    {
        glog.is(WARN) && glog << group("pAcommsHandler")
                              << "Failed to translate or queue message: " << e.what() << std::endl;
    }
}

void goby::apps::moos::CpAcommsHandler::driver_reset(
    std::shared_ptr<goby::acomms::ModemDriverBase> driver,
    const goby::acomms::ModemDriverException& e,
    pAcommsHandlerConfig::DriverFailureApproach::DriverFailureTechnique
        technique /* = cfg_.driver_failure_approach().technique() */)
{
    glog.is(WARN) && glog << group("pAcommsHandler") << "Driver exception: " << e.what()
                          << std::endl;
    glog.is(WARN) && glog << group("pAcommsHandler") << "Shutting down driver: " << driver
                          << std::endl;
    driver->shutdown();

    switch (technique)
    {
        case pAcommsHandlerConfig::DriverFailureApproach::DISABLE_AND_MOVE_LISTEN_DRIVER_TO_PRIMARY:
        case pAcommsHandlerConfig::DriverFailureApproach::MOVE_LISTEN_DRIVER_TO_PRIMARY:
        {
            if (driver == driver_)
            {
                glog.is(WARN) && glog << group("pAcommsHandler")
                                      << "Now using listen driver as new primary." << std::endl;
                // unbind signals to old driver
                driver_unbind();

                if (drivers_.size() == 1)
                {
                    glog.is(DIE) && glog << "No more drivers to try..." << std::endl;
                }
                else
                {
                    std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                             goby::acomms::protobuf::DriverConfig*>::iterator old_it =
                        drivers_.find(driver);
                    std::map<std::shared_ptr<goby::acomms::ModemDriverBase>,
                             goby::acomms::protobuf::DriverConfig*>::iterator new_it = old_it;

                    // try the next one after the current driver_, otherwise the first driver
                    ++new_it;
                    if (new_it == drivers_.end())
                        new_it = drivers_.begin();

                    // new primary driver_
                    driver_ = new_it->first;
                    if (!driver_restart_time_.count(driver_))
                        driver_->shutdown();

                    goby::acomms::protobuf::DriverConfig& new_config = *(new_it->second);
                    goby::acomms::protobuf::DriverConfig& old_config = *(old_it->second);

                    // swap the modem ids
                    int new_id = old_config.modem_id();
                    old_config.set_modem_id(new_config.modem_id());
                    new_config.set_modem_id(new_id);

                    // bind the correct signals
                    driver_bind();

                    // restart the new primary driver (after backoff)
                    driver_restart_time_.insert(std::make_pair(
                        driver_, goby::time::SystemClock::now<goby::time::SITime>() /
                                         boost::units::si::seconds +
                                     cfg_.driver_failure_approach().new_driver_backoff_sec()));
                }
            }

            if (technique == pAcommsHandlerConfig::DriverFailureApproach::
                                 DISABLE_AND_MOVE_LISTEN_DRIVER_TO_PRIMARY)
            {
                // erase old driver
                drivers_.erase(driver);
                driver_restart_time_.erase(driver);
                break;
            }
        }
        // fall through intentional (no break) - want to restart old driver if MOVE_LISTEN_DRIVER_TO_PRIMARY
        case pAcommsHandlerConfig::DriverFailureApproach::CONTINUALLY_RESTART_DRIVER:
        {
            glog.is(WARN) && glog << group("pAcommsHandler") << "Attempting to restart driver in "
                                  << cfg_.driver_failure_approach().driver_backoff_sec()
                                  << " seconds." << std::endl;
            driver_restart_time_.insert(
                std::make_pair(driver, goby::time::SystemClock::now<goby::time::SITime>() /
                                               boost::units::si::seconds +
                                           cfg_.driver_failure_approach().driver_backoff_sec()));
        }
        break;
    }
}

void goby::apps::moos::CpAcommsHandler::restart_drivers()
{
    double now = goby::time::SystemClock::now<goby::time::SITime>() / boost::units::si::seconds;
    std::set<std::shared_ptr<goby::acomms::ModemDriverBase>> drivers_to_start;

    for (std::map<std::shared_ptr<goby::acomms::ModemDriverBase>, double>::iterator it =
             driver_restart_time_.begin();
         it != driver_restart_time_.end();)
    {
        if (it->second < now)
        {
            drivers_to_start.insert(it->first);
            driver_restart_time_.erase(it++);
        }
        else
        {
            ++it;
        }
    }

    for (std::set<std::shared_ptr<goby::acomms::ModemDriverBase>>::iterator
             it = drivers_to_start.begin(),
             end = drivers_to_start.end();
         it != end; ++it)
    {
        std::shared_ptr<goby::acomms::ModemDriverBase> driver = *it;
        try
        {
            glog.is(DEBUG1) && glog << "Starting up driver: " << driver << std::endl;
            driver->startup(*drivers_[driver]);
        }
        catch (goby::acomms::ModemDriverException& e)
        {
            driver_reset(driver, e);
        }
    }
}

void goby::apps::moos::CpAcommsHandler::driver_bind()
{
    // bind the lower level pieces of goby-acomms together
    if (driver_)
    {
        goby::acomms::bind(mac_, *driver_);

        // informational 'driver' signals
        goby::acomms::connect(&driver_->signal_receive,
                              boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                          cfg_.moos_var().driver_receive(), _1, ""));

        goby::acomms::connect(&driver_->signal_transmit_result,
                              boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                          cfg_.moos_var().driver_transmit(), _1, ""));

        goby::acomms::connect(&driver_->signal_raw_incoming,
                              boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                          cfg_.moos_var().driver_raw_msg_in(), _1, ""));
        goby::acomms::connect(&driver_->signal_raw_outgoing,
                              boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                          cfg_.moos_var().driver_raw_msg_out(), _1, ""));

        goby::acomms::connect(
            &driver_->signal_raw_incoming,
            boost::bind(&CpAcommsHandler::handle_raw, this, _1, cfg_.moos_var().driver_raw_in()));

        goby::acomms::connect(
            &driver_->signal_raw_outgoing,
            boost::bind(&CpAcommsHandler::handle_raw, this, _1, cfg_.moos_var().driver_raw_out()));
    }
}

void goby::apps::moos::CpAcommsHandler::driver_unbind()
{
    // unbind the lower level pieces of goby-acomms together
    if (driver_)
    {
        goby::acomms::unbind(mac_, *driver_);

        // informational 'driver' signals
        goby::acomms::disconnect(&driver_->signal_receive,
                                 boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                             cfg_.moos_var().driver_receive(), _1, ""));

        goby::acomms::disconnect(&driver_->signal_transmit_result,
                                 boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                             cfg_.moos_var().driver_transmit(), _1, ""));

        goby::acomms::disconnect(&driver_->signal_raw_incoming,
                                 boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                             cfg_.moos_var().driver_raw_msg_in(), _1, ""));
        goby::acomms::disconnect(&driver_->signal_raw_outgoing,
                                 boost::bind(&CpAcommsHandler::handle_goby_signal, this, _1,
                                             cfg_.moos_var().driver_raw_msg_out(), _1, ""));

        goby::acomms::disconnect(
            &driver_->signal_raw_incoming,
            boost::bind(&CpAcommsHandler::handle_raw, this, _1, cfg_.moos_var().driver_raw_in()));

        goby::acomms::disconnect(
            &driver_->signal_raw_outgoing,
            boost::bind(&CpAcommsHandler::handle_raw, this, _1, cfg_.moos_var().driver_raw_out()));
    }
}
