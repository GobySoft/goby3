// Copyright 2017-2020:
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

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/moos/moos_translator.h"
#include "goby/moos/protobuf/goby_moos_app.pb.h"
#include "moos_plugin_translator.h"

using goby::apps::moos::protobuf::GobyMOOSGatewayConfig;

goby::moos::Translator::Translator(const GobyMOOSGatewayConfig& config)
    : goby::middleware::SimpleThread<GobyMOOSGatewayConfig>(
          config, 10 * boost::units::si::hertz) // config.poll_frequency())
{
    goby::moos::protobuf::GobyMOOSAppConfig moos_cfg;
    if (cfg().moos().has_use_binary_protobuf())
        moos_cfg.set_use_binary_protobuf(cfg().moos().use_binary_protobuf());
    if (cfg().moos().moos_parser_technique())
        moos_cfg.set_moos_parser_technique(cfg().moos().moos_parser_technique());
    goby::moos::set_moos_technique(moos_cfg);

    if (config.app().simulation().time().use_sim_time())
        SetMOOSTimeWarp(config.app().simulation().time().warp_factor());

    moos_.comms().SetOnConnectCallBack(TranslatorOnConnectCallBack, this);

    moos_.comms().Run(cfg().moos().server(), cfg().moos().port(), translator_name());
}

void goby::moos::Translator::loop() { moos_.loop(); }

bool goby::moos::TranslatorOnConnectCallBack(void* translator)
{
    reinterpret_cast<goby::moos::Translator*>(translator)->moos().on_connect();
    return true;
}

void goby::moos::Translator::MOOSInterface::on_connect()
{
    using goby::glog;
    using namespace goby::util::logger;

    for (const auto& var_func_pair : trigger_vars_)
    {
        const std::string& moos_var = var_func_pair.first;
        comms_.Register(moos_var);
        glog.is(DEBUG1) && glog << "Subscribed for MOOS variable: " << moos_var << std::endl;
    }

    for (const std::string& moos_var : buffer_vars_)
    {
        comms_.Register(moos_var);
        glog.is(DEBUG1) && glog << "Subscribed for MOOS variable: " << moos_var << std::endl;
    }
}

void goby::moos::Translator::MOOSInterface::loop()
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
        auto it = trigger_vars_.find(msg.GetKey());
        if (it != trigger_vars_.end())
            it->second(msg);
    }
}
