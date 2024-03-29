// Copyright 2017-2022:
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

#include <chrono>  // for operator>, seconds
#include <iomanip> // for operator<<, set...
#include <limits>  // for numeric_limits

#include <MOOS/libMOOS/Comms/CommsTypes.h>           // for MOOSMSG_LIST
#include <MOOS/libMOOS/Comms/MOOSAsyncCommClient.h>  // for MOOSAsyncCommCl...
#include <MOOS/libMOOS/Comms/MOOSCommClient.h>       // for CMOOSCommClient
#include <MOOS/libMOOS/Utils/MOOSUtilityFunctions.h> // for MOOSTime, SetMO...

#include "goby/middleware/protobuf/app_config.pb.h" // for AppConfig, AppC...
#include "goby/moos/moos_protobuf_helpers.h"        // for set_moos_technique
#include "goby/moos/protobuf/goby_moos_app.pb.h"    // for GobyMOOSAppConfig
#include "goby/time/convert.h"                      // for SystemClock::now
#include "goby/time/types.h"                        // for SITime
#include "goby/util/debug_logger/flex_ostream.h"    // for operator<<, glog
#include "goby/util/debug_logger/flex_ostreambuf.h" // for DEBUG1, logger

#include "moos_plugin_translator.h"

using goby::apps::moos::protobuf::GobyMOOSGatewayConfig;

goby::moos::TranslatorBase::TranslatorBase(const GobyMOOSGatewayConfig& config) : cfg_(config)

{
    goby::moos::protobuf::GobyMOOSAppConfig moos_cfg;
    if (cfg_.moos().has_use_binary_protobuf())
        moos_cfg.set_use_binary_protobuf(cfg_.moos().use_binary_protobuf());
    if (cfg_.moos().moos_parser_technique())
        moos_cfg.set_moos_parser_technique(cfg_.moos().moos_parser_technique());
    goby::moos::set_moos_technique(moos_cfg);

    if (config.app().simulation().time().use_sim_time())
        SetMOOSTimeWarp(config.app().simulation().time().warp_factor());

    moos_.comms().SetOnConnectCallBack(TranslatorOnConnectCallBack, this);

    moos_.comms().Run(cfg_.moos().server(), cfg_.moos().port(), translator_name());
}

void goby::moos::TranslatorBase::loop() { moos_.loop(); }

bool goby::moos::TranslatorOnConnectCallBack(void* translator)
{
    reinterpret_cast<goby::moos::TranslatorBase*>(translator)->moos().on_connect();
    return true;
}

void goby::moos::TranslatorBase::MOOSInterface::on_connect()
{
    using goby::glog;
    using namespace goby::util::logger;

    for (const auto& var_func_pair : trigger_vars_) moos_register(var_func_pair.first);
    for (const std::string& moos_var : buffer_vars_) moos_register(moos_var);
    for (const auto& filter_func_pair : trigger_wildcard_vars_)
        moos_wildcard_register(filter_func_pair.first);
    connected_ = true;
}

void goby::moos::TranslatorBase::MOOSInterface::loop()
{
    using goby::glog;
    using namespace goby::util::logger;

    auto now = goby::time::SystemClock::now();
    if (now > next_time_publish_)
    {
        // older MOOSDBs disconnect on time warp without regular comms
        std::stringstream ss;
        ss << std::setprecision(std::numeric_limits<double>::digits10) << "moostime=" << MOOSTime()
           << ",gobytime=" << goby::time::SystemClock::now<goby::time::SITime>().value()
           << std::endl;
        comms_.Notify("GOBY_MOOS_TRANSLATOR_TIME", ss.str());
        next_time_publish_ += std::chrono::seconds(1);
    }

    MOOSMSG_LIST moos_msgs;
    comms_.Fetch(moos_msgs);
    // buffer all then trigger
    for (const CMOOSMsg& msg : moos_msgs)
    {
        if (buffer_vars_.count(msg.GetKey()))
        {
            const auto& key = msg.GetKey();
            buffer_[key] = msg;
        }
    }
    for (const CMOOSMsg& msg : moos_msgs)
    {
        {
            auto it = trigger_vars_.find(msg.GetKey());
            if (it != trigger_vars_.end())
                it->second(msg);
        }

        for (const auto& filter_func_pair : trigger_wildcard_vars_)
        {
            const MOOS::MsgFilter& filter = filter_func_pair.first;
            if (filter.Matches(msg))
                filter_func_pair.second(msg);
        }
    }
}
