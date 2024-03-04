// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_JANUS_DRIVER_H
#define GOBY_ACOMMS_MODEMDRIVER_JANUS_DRIVER_H

#include <cstdint> // for uint8_t
#include <map>      // for map
#include <string>   // for string
#include <vector>   // for vector

#include "goby/acomms/modemdriver/driver_base.h"   // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"   // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmission
#include "goby/acomms/protobuf/janus_driver.pb.h" // for Config, MessageTy...
#include "goby/util/thirdparty/nlohmann/json.hpp"  // for json

extern "C" {
#include <janus/janus.h>
#include <janus/defaults.h>
#include <janus/simple_tx.h>
#include <janus/simple_rx.h>
#include <janus/rx.h>
#include <janus/dump.h>
#include <janus/parameters.h>
#include <janus/utils/go_cfar.h>
}

// todo: probably should move this inside the class
struct janus_rx_msg_pkt {

      int cargo_size;
      std::string cargo_hex;
      std::string cargo;
      int station_id;
      int destination_id;
      bool ack_request;
      int payload_size;

};
namespace goby
{
namespace acomms
{
namespace protobuf
{
class ModemTransmission;
} // namespace protobuf

/// \brief provides an API to the imaginary ABC modem (as an example how to write drivers)
/// \ingroup acomms_api
///

class JanusDriver : public ModemDriverBase
{
  public:
    JanusDriver();
    ~JanusDriver() override;
    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;
    void handle_ack_transmission(const protobuf::ModemTransmission& m);
    void pad_message(std::vector<uint8_t> &vec);
    void send_janus_packet(const protobuf::ModemTransmission& msg, std::vector<std::uint8_t> payload, bool ack = false);
    void append_crc16(std::vector<std::uint8_t> &vec);
    janus_rx_msg_pkt parse_janus_packet(const janus_packet_t pkt, bool verbosity);
    void to_modem_transmission(janus_rx_msg_pkt packet,protobuf::ModemTransmission& msg);
    std::uint8_t get_goby_header(const protobuf::ModemTransmission& msg);
    std::uint8_t CreateGobyHeader(const protobuf::ModemTransmission& m);
    void DecodeGobyHeader(std::uint8_t header, protobuf::ModemTransmission& m);
    janus_parameters_t get_rx_params();
    unsigned int get_frame_num(std::string cargo);
    janus_simple_tx_t init_janus_tx();
    janus_simple_rx_t init_janus_rx();
    void send_ack(unsigned int src, unsigned int dest,unsigned int frame_number);
    int verbosity;
    std::string pset_file;
    std::string tx_device;
    std::string rx_device;
    int tx_pset_id;
    int rx_pset_id;
    int class_id;
    int application_type;
    int tx_channels;
    int rx_channels;
    int sample_rate;
    unsigned int acomms_id;
    std::uint32_t next_frame_{0};
    static constexpr int DEFAULT_MTU_BYTES{1024};
    janus_parameters_t params_tx = janus_parameters_new();
    janus_parameters_t params_rx;
    janus_simple_tx_t  simple_tx;
    janus_simple_rx_t  simple_rx;
    janus_packet_t packet_rx = 0;
    janus_rx_state_t state_rx = 0;
    unsigned queried_detection_time;
    janus_carrier_sensing_t carrier_sensing;
    protobuf::ModemTransmission modem_msg;

  private:
    const janus::protobuf::Config& janus_driver_cfg() const
    {
        return driver_cfg_.GetExtension(janus::protobuf::config);
    }

  private:
    enum
    {
        DEFAULT_BAUD = 4800
    };


    protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
    // rest is up to you!
};
} // namespace acomms
} // namespace goby
#endif