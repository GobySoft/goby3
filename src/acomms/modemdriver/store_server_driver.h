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

#ifndef GOBY_ACOMMS_MODEMDRIVER_STORE_SERVER_DRIVER_H
#define GOBY_ACOMMS_MODEMDRIVER_STORE_SERVER_DRIVER_H

#include "goby/time/system_clock.h"

#include "goby/acomms/modemdriver/driver_base.h"
#include "goby/acomms/modemdriver/iridium_rudics_packet.h"
#include "goby/acomms/protobuf/store_server.pb.h"
#include "goby/acomms/protobuf/store_server_driver.pb.h"

namespace goby
{
namespace acomms
{
class StoreServerDriver : public ModemDriverBase
{
  public:
    StoreServerDriver();
    void startup(const protobuf::DriverConfig& cfg);
    void shutdown();
    void do_work();
    void handle_initiate_transmission(const protobuf::ModemTransmission& m);

    constexpr static const char* eol{"\r"};
    constexpr static int default_port{11244};

    template <typename StoreServerMessage>
    static void parse_store_server_message(const std::string& bytes, StoreServerMessage* msg)
    {
        std::string pb_encoded;
        goby::acomms::parse_rudics_packet(&pb_encoded, bytes, std::string(eol));
        msg->ParseFromString(pb_encoded);
    }

    template <typename StoreServerMessage>
    static void serialize_store_server_message(const StoreServerMessage& msg, std::string* bytes)
    {
        std::string pb_encoded;
        msg.SerializeToString(&pb_encoded);
        goby::acomms::serialize_rudics_packet(pb_encoded, bytes, std::string(eol));
    }

  private:
    void handle_response(const protobuf::StoreServerResponse& response);

  private:
    protobuf::DriverConfig driver_cfg_;
    store_server::protobuf::Config store_server_driver_cfg_;
    protobuf::StoreServerRequest request_;
    std::uint64_t last_send_time_;
    double query_interval_seconds_;
    double reset_interval_seconds_;
    bool waiting_for_reply_;
    std::uint32_t next_frame_;
};
} // namespace acomms
} // namespace goby
#endif
