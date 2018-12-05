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

#include "moos_plugin_translator.h"

goby::moos::Translator::Translator(const GobyMOOSGatewayConfig& config)
    : goby::SimpleThread<GobyMOOSGatewayConfig>(config, config.poll_frequency())
{
    moos_comms_.SetOnConnectCallBack(TranslatorOnConnectCallBack, this);
    moos_comms_.Run(cfg().moos_server(), cfg().moos_port(), translator_name(),
                    cfg().poll_frequency());
}

void goby::moos::Translator::moos_on_connect()
{
    using goby::glog;
    using namespace goby::common::logger;

    for (const std::string& moos_var : moos_trigger_vars_)
    {
        moos_comms_.Register(moos_var);
        glog.is(DEBUG1) && glog << "Subscribed for MOOS variable: " << moos_var << std::endl;
    }

    for (const std::string& moos_var : moos_buffer_vars_)
    {
        moos_comms_.Register(moos_var);
        glog.is(DEBUG1) && glog << "Subscribed for MOOS variable: " << moos_var << std::endl;
    }
}

void goby::moos::Translator::loop()
{
    using goby::glog;
    using namespace goby::common::logger;

    MOOSMSG_LIST moos_msgs;
    moos_comms_.Fetch(moos_msgs);
    // buffer all then trigger
    for (const CMOOSMsg& msg : moos_msgs)
    {
        if (moos_buffer_vars_.count(msg.GetKey()))
        {
            const auto& key = msg.GetKey();
            moos_buffer_[key] = msg;
        }
    }
    for (const CMOOSMsg& msg : moos_msgs)
    {
        if (moos_trigger_vars_.count(msg.GetKey()))
            moos_to_goby(msg);
    }
}

bool goby::moos::TranslatorOnConnectCallBack(void* translator)
{
    reinterpret_cast<goby::moos::Translator*>(translator)->moos_on_connect();
    return true;
}
