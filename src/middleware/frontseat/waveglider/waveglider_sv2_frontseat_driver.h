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

#ifndef WavegliderSV2FrontSeat20150909H
#define WavegliderSV2FrontSeat20150909H

#include <boost/bimap.hpp>
#include <boost/circular_buffer.hpp>
#include <dccl.h>

#include "goby/util/linebasedcomms/tcp_client.h"

#include "goby/middleware/frontseat/interface.h"

#include "goby/middleware/frontseat/waveglider/waveglider_sv2_frontseat_driver.pb.h"
#include "goby/middleware/frontseat/waveglider/waveglider_sv2_frontseat_driver_config.pb.h"

#include "waveglider_sv2_serial_client.h"

namespace goby
{
namespace middleware
{
namespace frontseat
{
class WavegliderSV2 : public InterfaceBase
{
  public:
    WavegliderSV2(const protobuf::Config& cfg);

  private: // virtual methods from InterfaceBase
    void loop() override;

    void send_command_to_frontseat(const protobuf::CommandRequest& command) override;
    void send_data_to_frontseat(const protobuf::InterfaceData& data) override;
    void send_raw_to_frontseat(const protobuf::Raw& data) override;
    protobuf::FrontSeatState frontseat_state() const override;
    bool frontseat_providing_data() const override;

    void handle_sv2_message(const std::string& message);
    void handle_enumeration_request(const protobuf::SV2RequestEnumerate& msg);
    void handle_request_status(const protobuf::SV2RequestStatus& request);
    void handle_request_queued_message(const protobuf::SV2RequestQueuedMessage& request);

    void check_crc(const std::string& message, uint16_t expected);
    void add_crc(std::string* message);
    void encode_and_write(const google::protobuf::Message& message);

  private: // internal non-virtual methods
    void check_connection_state();

  private:
    const protobuf::WavegliderSV2Config waveglider_sv2_config_;

    bool frontseat_providing_data_;
    goby::time::SystemClock::time_point last_frontseat_data_time_;
    protobuf::FrontSeatState frontseat_state_;

    boost::asio::io_service io_;
    std::shared_ptr<SV2SerialConnection> serial_;

    boost::circular_buffer<std::shared_ptr<protobuf::SV2CommandFollowFixedHeading>>
        queued_messages_;

    dccl::Codec dccl_;
};
} // namespace frontseat
} // namespace middleware
} // namespace goby

#endif
