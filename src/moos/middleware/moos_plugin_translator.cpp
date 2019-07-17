// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#include "moos_plugin_translator.h"
#include "goby/moos/moos_translator.h"
#include "goby/moos/protobuf/goby_moos_app.pb.h"

using goby::apps::moos::protobuf::GobyMOOSGatewayConfig;

goby::moos::Translator::Translator(const GobyMOOSGatewayConfig& config)
    : goby::middleware::SimpleThread<GobyMOOSGatewayConfig>(config, config.poll_frequency())
{
    goby::moos::protobuf::GobyMOOSAppConfig moos_cfg;
    if (cfg().moos().has_use_binary_protobuf())
        moos_cfg.set_use_binary_protobuf(cfg().moos().use_binary_protobuf());
    if (cfg().moos().moos_parser_technique())
        moos_cfg.set_moos_parser_technique(cfg().moos().moos_parser_technique());
    goby::moos::set_moos_technique(moos_cfg);

    moos_.comms().SetOnConnectCallBack(TranslatorOnConnectCallBack, this);

    moos_.comms().Run(cfg().moos().server(), cfg().moos().port(), translator_name(),
                      cfg().poll_frequency());
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
