// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Henrik Schmidt <henrik@mit.edu>
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

#include <algorithm>   // for max
#include <chrono>      // for operator+
#include <cstdlib>     // for abs
#include <dlfcn.h>     // for dlopen
#include <map>         // for multimap
#include <memory>      // for shared...
#include <ostream>     // for basic_...
#include <string>      // for string
#include <type_traits> // for __succ...
#include <utility>     // for pair

#include <MOOS/libMOOS/Comms/CommsTypes.h>         // for MOOSMS...
#include <MOOS/libMOOS/Comms/MOOSMsg.h>            // for CMOOSMsg
#include <boost/bind.hpp>                          // for bind_t
#include <boost/program_options/variables_map.hpp> // for variab...
#include <boost/signals2/expired_slot.hpp>         // for expire...
#include <boost/smart_ptr/shared_ptr.hpp>          // for shared...
#include <boost/system/error_code.hpp>             // for error_...
#include <dccl/dynamic_protobuf_manager.h>         // for Dynami...
#include <google/protobuf/message.h>               // for Message

#include "goby/apps/moos/pTranslator/pTranslator_config.pb.h" // for pTrans...
#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/moos/dynamic_moos_vars.h"                      // for Dynami...
#include "goby/moos/moos_protobuf_helpers.h"                  // for dynami...
#include "goby/moos/moos_string.h"                            // for operat...
#include "goby/moos/protobuf/goby_moos_app.pb.h"              // for GobyMO...
#include "goby/moos/protobuf/translator.pb.h"                 // for Transl...
#include "goby/time/io.h"                                     // for operat...
#include "goby/util/debug_logger/flex_ostream.h"              // for operat...
#include "goby/util/debug_logger/flex_ostreambuf.h"           // for VERBOSE
#include "goby/util/debug_logger/logger_manipulators.h"       // for die

#include "pTranslator.h"

using goby::glog;
using namespace goby::util::logger;
using goby::moos::operator<<;
using goby::apps::moos::protobuf::pTranslatorConfig;

pTranslatorConfig goby::apps::moos::CpTranslator::cfg_;
goby::apps::moos::CpTranslator* goby::apps::moos::CpTranslator::inst_ = nullptr;

goby::apps::moos::CpTranslator* goby::apps::moos::CpTranslator::get_instance()
{
    if (!inst_)
        inst_ = new goby::apps::moos::CpTranslator();
    return inst_;
}

void goby::apps::moos::CpTranslator::delete_instance() { delete inst_; }

goby::apps::moos::CpTranslator::CpTranslator()
    : goby::moos::GobyMOOSApp(&cfg_),
      translator_(cfg_.translator_entry(), cfg_.common().lat_origin(), cfg_.common().lon_origin(),
                  cfg_.modem_id_lookup_path()),
      lat_origin_(std::numeric_limits<double>::quiet_NaN()),
      lon_origin_(std::numeric_limits<double>::quiet_NaN()),
      new_origin_(false),
      work_(timer_io_context_)
{
    dccl::DynamicProtobufManager::enable_compilation();

    // load all shared libraries
    for (int i = 0, n = cfg_.load_shared_library_size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << "Loading shared library: " << cfg_.load_shared_library(i)
                                 << std::endl;

        void* handle = dlopen(cfg_.load_shared_library(i).c_str(), RTLD_LAZY);
        if (!handle)
        {
            glog << die << "Failed ... check path provided or add to /etc/ld.so.conf "
                 << "or LD_LIBRARY_PATH" << std::endl;
        }
    }

    // load all .proto files
    for (int i = 0, n = cfg_.load_proto_file_size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << "Loading protobuf file: " << cfg_.load_proto_file(i)
                                 << std::endl;

        if (!dccl::DynamicProtobufManager::find_descriptor(cfg_.load_proto_file(i)))
            glog.is(DIE) && glog << "Failed to load file." << std::endl;
    }

    // process translator entries
    for (int i = 0, n = cfg_.translator_entry_size(); i < n; ++i)
    {
        typedef std::shared_ptr<google::protobuf::Message> GoogleProtobufMessagePointer;
        glog.is(VERBOSE) && glog << "Checking translator entry: "
                                 << cfg_.translator_entry(i).DebugString() << std::flush;

        // check that the protobuf file is loaded somehow
        dccl::DynamicProtobufManager::new_protobuf_message<GoogleProtobufMessagePointer>(
            cfg_.translator_entry(i).protobuf_name());

        if (cfg_.translator_entry(i).trigger().type() ==
            goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_PUBLISH)
        {
            // subscribe for trigger publish variables
            GobyMOOSApp::subscribe(
                cfg_.translator_entry(i).trigger().moos_var(),
                boost::bind(&CpTranslator::create_on_publish, this, _1, cfg_.translator_entry(i)));
        }
        else if (cfg_.translator_entry(i).trigger().type() ==
                 goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_TIME)
        {
            timers_.push_back(std::make_shared<Timer>(timer_io_context_));

            Timer& new_timer = *timers_.back();

            new_timer.expires_from_now(
                std::chrono::seconds(cfg_.translator_entry(i).trigger().period()));
            // Start an asynchronous wait.
            new_timer.async_wait(boost::bind(&CpTranslator::create_on_timer, this, _1,
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
                               &CpTranslator::create_on_multiplex_publish, this);
    }

    // Dynamic UTM. H. Schmidt 7/30/21
    GobyMOOSApp::subscribe("LAT_ORIGIN", &CpTranslator::handle_lat_origin, this);
    GobyMOOSApp::subscribe("LONG_ORIGIN", &CpTranslator::handle_lon_origin, this);

}

goby::apps::moos::CpTranslator::~CpTranslator() = default;

void goby::apps::moos::CpTranslator::handle_lat_origin(const CMOOSMsg& msg)
{
 double new_lat = msg.GetDouble();
 if (!isnan(new_lat))
   {
     lat_origin_ = new_lat;
     new_origin_ = true;
   }
}

void goby::apps::moos::CpTranslator::handle_lon_origin(const CMOOSMsg& msg)
{
 double new_lon = msg.GetDouble();
 if (!isnan(new_lon))
   {
     lon_origin_ = new_lon;
     new_origin_ = true;
   }
}

void goby::apps::moos::CpTranslator::loop()
{
    if (new_origin_ && !isnan(lat_origin_) && !isnan(lon_origin_))
    {
        translator_.update_utm_datum(lat_origin_, lon_origin_);
        new_origin_ = false;
    }

  timer_io_context_.poll();
}

void goby::apps::moos::CpTranslator::create_on_publish(
    const CMOOSMsg& trigger_msg, const goby::moos::protobuf::TranslatorEntry& entry)
{
    glog.is(VERBOSE) && glog << "Received trigger: " << trigger_msg << std::endl;

    if (!entry.trigger().has_mandatory_content() ||
        trigger_msg.GetString().find(entry.trigger().mandatory_content()) != std::string::npos)
        do_translation(entry);
    else
        glog.is(VERBOSE) &&
            glog << "Message missing mandatory content for: " << entry.protobuf_name() << std::endl;
}

void goby::apps::moos::CpTranslator::create_on_multiplex_publish(const CMOOSMsg& moos_msg)
{
    std::shared_ptr<google::protobuf::Message> msg = dynamic_parse_for_moos(moos_msg.GetString());

    if (!msg)
    {
        glog.is(WARN) && glog << "Multiplex receive failed: Unknown Protobuf type for "
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

        for (auto& it : out)
        {
            glog.is(VERBOSE) && glog << "Inverse Publishing: " << it.second.GetKey() << std::endl;
            publish(it.second);
        }
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << "Failed to inverse publish: " << e.what() << std::endl;
    }
}

void goby::apps::moos::CpTranslator::create_on_timer(
    const boost::system::error_code& error, const goby::moos::protobuf::TranslatorEntry& entry,
    Timer* timer)
{
    if (!error)
    {
        double skew_seconds = std::abs((goby::time::SystemClock::now() - timer->expires_at()) /
                                       std::chrono::seconds(1));
        if (skew_seconds > ALLOWED_TIMER_SKEW_SECONDS)
        {
            glog.is(VERBOSE) && glog << "clock skew of " << skew_seconds
                                     << " seconds detected, resetting timer." << std::endl;
            timer->expires_at(goby::time::SystemClock::now() +
                              std::chrono::seconds(entry.trigger().period()));
        }
        else
        {
            // reset the timer
            timer->expires_at(timer->expires_at() + std::chrono::seconds(entry.trigger().period()));
        }

        timer->async_wait(boost::bind(&CpTranslator::create_on_timer, this, _1, entry, timer));

        glog.is(VERBOSE) && glog << "Received trigger for: " << entry.protobuf_name() << std::endl;
        glog.is(VERBOSE) && glog << "Next expiry: " << timer->expires_at() << std::endl;

        do_translation(entry);
    }
}

void goby::apps::moos::CpTranslator::do_translation(
    const goby::moos::protobuf::TranslatorEntry& entry)
{
    std::shared_ptr<google::protobuf::Message> created_message =
        translator_.moos_to_protobuf<std::shared_ptr<google::protobuf::Message> >(
            dynamic_vars().all(), entry.protobuf_name());

    glog.is(DEBUG1) && glog << "Created message: \n" << created_message->DebugString() << std::endl;

    do_publish(created_message);
}

void goby::apps::moos::CpTranslator::do_publish(
    const std::shared_ptr<google::protobuf::Message>& created_message)
{
    std::multimap<std::string, CMOOSMsg> out;

    out = translator_.protobuf_to_moos(*created_message);

    for (auto& it : out)
    {
        glog.is(VERBOSE) && glog << "Publishing: " << it.second << std::endl;
        publish(it.second);
    }
}
