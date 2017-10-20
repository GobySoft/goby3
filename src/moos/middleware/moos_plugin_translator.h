// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef MOOS_PLUGIN_TRANSLATOR_20171020H
#define MOOS_PLUGIN_TRANSLATOR_20171020H

#include "goby/moos/moos_header.h"
#include "goby/moos/protobuf/moos_gateway_config.pb.h"
#include "goby/middleware/multi-thread-application.h"

namespace goby
{
    namespace moos
    {
        template<typename Derived>
            class Translator : public goby::SimpleThread<GobyMOOSGatewayConfig>
        {
        public:
        Translator(const GobyMOOSGatewayConfig& config) :
            goby::SimpleThread<GobyMOOSGatewayConfig>(config, cfg().moos_comms_tick())
            {
                moos_comms_.Run(cfg().moos_server(),
                                cfg().moos_port(),
                                translator_name(),
                                cfg().moos_comms_tick());
            }
            
            
        protected:

            virtual std::string translator_name()
            {
                return std::string("goby::moos::Translator::" + std::to_string(reinterpret_cast<uintptr_t>(this)));
            }

            // Goby
            typename goby::SimpleThread<GobyMOOSGatewayConfig>::Transporter& goby_comms() { return interprocess(); }
            
            // MOOS
            
            void add_moos_trigger(const std::string& moos_var)
            { moos_trigger_vars_.insert(moos_var); }
            
            void add_moos_buffer(const std::string& moos_var)
            { moos_buffer_vars_.insert(moos_var); }

            virtual void moos_to_goby(const CMOOSMsg& moos_msg) = 0;
            
            CMOOSCommClient& moos_comms()
            { return moos_comms_; }

            std::map<std::string, CMOOSMsg>& moos_buffer()
            { return moos_buffer_; }            
            
        private:
            void loop() override
            {
                MOOSMSG_LIST moos_msgs;
                moos_comms_.Fetch(moos_msgs);
                // buffer all then trigger
                for(const CMOOSMsg& msg : moos_msgs)
                {
                    if(moos_buffer_vars_.count(msg.GetKey()))
                        moos_buffer_[msg.GetKey()] = msg;
                }
                for(const CMOOSMsg& msg : moos_msgs)
                {
                    if(moos_trigger_vars_.count(msg.GetKey()))
                        moos_to_goby(msg);
                }
            }
            
        private:
            CMOOSCommClient moos_comms_;
            std::set<std::string> moos_trigger_vars_;
            std::set<std::string> moos_buffer_vars_;
            std::map<std::string, CMOOSMsg> moos_buffer_;
        };
    }
}




#endif
