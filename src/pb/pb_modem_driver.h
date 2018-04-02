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

#ifndef PBModemDriver20120404H
#define PBModemDriver20120404H

#include "goby/common/time.h"

#include "goby/acomms/modemdriver/driver_base.h"
#include "goby/pb/protobuf_node.h"
#include "goby/common/zeromq_service.h"
#include "goby/acomms/protobuf/store_server.pb.h"
#include "goby/pb/protobuf/pb_modem_driver.pb.h"


namespace goby
{
    namespace pb
    {
        class PBDriver : public acomms::ModemDriverBase,
            public goby::pb::StaticProtobufNode
        {
          public:
            PBDriver(goby::common::ZeroMQService* zeromq_service);
            void startup(const acomms::protobuf::DriverConfig& cfg);
            void shutdown();            
            void do_work();
            void handle_initiate_transmission(const acomms::protobuf::ModemTransmission& m);

          private:
            void handle_response(const acomms::protobuf::StoreServerResponse& response);
            
          private:            
            goby::common::ZeroMQService* zeromq_service_;
            acomms::protobuf::DriverConfig driver_cfg_;
	    common::protobuf::ZeroMQServiceConfig service_cfg_;
            acomms::protobuf::StoreServerRequest request_;
            std::uint64_t last_send_time_;
            int request_socket_id_;
            double query_interval_seconds_;
            double reset_interval_seconds_;
            bool waiting_for_reply_;
            std::uint32_t next_frame_;
        };
    }
}
#endif
