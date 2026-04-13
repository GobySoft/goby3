// Copyright 2020-2025:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Thomas McCabe <tom.mccabe@missionsystems.com.au>
//   Toby Schneider <toby@gobysoft.org>
//   Jared Silbermann <jared.silbermann@missionsystems.com.au>
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
#include <map>     // for map
#include <string>  // for string
#include <vector>  // for vector

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
    void popoto_update_power(protobuf::ModemTransmission& msg);
    void send_wake(void);
    void send_range_request(int dest);
    void popoto_sleep(void);

  private:
    void parse_in(const std::string& in, std::map<std::string, std::string>* out);

    void set_popoto_value(const std::string& key, int val)
    {
        send_popoto_command("SetValue", key + " int " + std::to_string(val));
    }
    void set_popoto_value(const std::string& key, float val)
    {
        send_popoto_command("SetValue", key + " float " + std::to_string(val));
    }

    void get_popoto_value(const std::string& key) { send_popoto_command("GetValue", key); }

    void send_popoto_command(const std::string& command)
    {
        // https://github.com/Delresearch/PopotoAPI/blob/2b511ff2109b6cde85b2261aec414882a332e8eb/CPP/popoto_client/include/TCPCmdClient.hpp#L50
        send_popoto_command(command, " Unused Arguments");
    }
    void send_popoto_command(const std::string& command, const nlohmann::json& args);

    std::uint8_t create_goby_header(const protobuf::ModemTransmission& m);
    void decode_goby_header(std::uint8_t header, protobuf::ModemTransmission& m);
    void decode_popoto_header(std::vector<uint8_t> data, protobuf::ModemTransmission& m);
    void process_popoto_json(const std::string& message, protobuf::ModemTransmission& modem_msg);

    const popoto::protobuf::Config& popoto_driver_cfg() const
    {
        return driver_cfg_.GetExtension(popoto::protobuf::config);
    }

    static std::string json_to_binary(const nlohmann::json& element)
    {
        std::string output;
        for (auto& subel : element) { output.append(1, (char)((uint8_t)subel)); }
        return output;
    }

    // Remove popoto trash from the incoming serial string
    static std::string clean_popoto_string(std::string in, std::string p)
    {
        std::string out = std::move(in);
        std::string::size_type n = p.length();
        for (std::string::size_type i = out.find(p); i != std::string::npos; i = out.find(p))
            out.erase(i, n);

        return out;
    }

  private:
    protobuf::DriverConfig driver_cfg_;
    int sender_id_{0};
    float modem_power_;
    std::uint32_t next_frame_{0};

    protobuf::ModemTransmission modem_msg_;
    bool modem_msg_complete_ = false;

    int application_type_;

    static constexpr int DEFAULT_BAUD{115200};
    static constexpr int DEFAULT_MTU_BYTES{1024};
    static constexpr int POPOTO_BROADCAST_ID{255};

    // Bitrates with Popoto modem: map these onto 0-5
    std::vector<std::string> rate_to_speed{"setRate80",   "setRate640",  "setRate1280",
                                           "setRate2560", "setRate5120", "setRate10240"};

    bool startup_done_{false};
};
} // namespace acomms
} // namespace goby

#endif
