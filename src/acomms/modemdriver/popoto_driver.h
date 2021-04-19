// Copyright 2020-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Thomas McCabe <tom.mccabe@missionsystems.com.au>
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

/************************************************************/
/*    NAME: Thomas McCabe                                   */
/*    ORGN: Mission Systems Pty Ltd                         */
/*    FILE: Popoto.h                                        */
/*    DATE: Aug 20 2020                                     */
/************************************************************/

/* Copyright (c) 2020 mission systems pty ltd */

#ifndef GOBY_ACOMMS_MODEMDRIVER_POPOTO_DRIVER_H
#define GOBY_ACOMMS_MODEMDRIVER_POPOTO_DRIVER_H

#include <cstddef> // for size_t
#include <cstdint> // for uint8_t
#include <map>      // for map
#include <string>   // for string
#include <vector>   // for vector

#include "goby/acomms/modemdriver/driver_base.h"   // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"   // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmission
#include "goby/acomms/protobuf/popoto_driver.pb.h" // for Config, MessageTy...
#include "goby/util/thirdparty/nlohmann/json.hpp"  // for json

namespace goby
{
namespace acomms
{
class PopotoDriver : public ModemDriverBase
{
  public:
    PopotoDriver();
    ~PopotoDriver() override;

    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;
    void send(protobuf::ModemTransmission& msg);
    void play_file(protobuf::ModemTransmission& msg);
    void send_ping(protobuf::ModemTransmission& msg);
    void send_wake(void);
    void send_range_request(int dest);
    void popoto_sleep(void);

  private:
    void parse_in(const std::string& in, std::map<std::string, std::string>* out);
    void signal_and_write(const std::string& raw);

    std::uint16_t CreateGobyHeader(const protobuf::ModemTransmission& m);

    void DecodeHeader(std::vector<uint8_t> data, protobuf::ModemTransmission& m);
    void DecodeGobyHeader(std::uint8_t header, std::uint8_t ack_num, protobuf::ModemTransmission& m);
    void ProcessJSON(const std::string& message,protobuf::ModemTransmission& modem_msg);
    std::string change_to_popoto_json(std::string input, size_t pos, std::string setval, std::string num_type);
    const popoto::protobuf::Config& popoto_driver_cfg() const
    {
        return driver_cfg_.GetExtension(popoto::protobuf::config);
    }

    static std::string json_to_binary(const nlohmann::json& element);
    static std::string binary_to_json(const std::uint8_t* buf, size_t num_bytes);
    static std::string StripString(std::string in, std::string p);

  private:
    protobuf::DriverConfig driver_cfg_;
    int sender_id_{0};
    std::uint32_t next_frame_{0};
    protobuf::ModemTransmission modem_msg_;

    static constexpr int DEFAULT_BAUD{115200};
    static constexpr int DEFAULT_MTU_BYTES{1024};
    static constexpr int POPOTO_BROADCAST_ID{255};

    enum GobyHeaderBits
    {
        GOBY_HEADER_TYPE = 0,       // 0 == Data, 1 == Ack
        GOBY_HEADER_ACK_REQUEST = 1 // 0 == no ack requested, 1 == ack requested
    };

    enum ConnectionType
    {
        SERIAL_CONNECTION = 0,       // 0 == Data, 1 == Ack
        ETHERNET_CONNECTION = 1 // 0 == no ack requested, 1 == ack requested
    }; ConnectionType myConnection;

};
} // namespace acomms
} // namespace goby

#endif
