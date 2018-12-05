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

#ifndef WavegliderSV2FrontSeat20150909H
#define WavegliderSV2FrontSeat20150909H

#include <boost/bimap.hpp>
#include <boost/circular_buffer.hpp>
#include <dccl.h>

#include "goby/util/linebasedcomms/tcp_client.h"

#include "goby/moos/frontseat/frontseat.h"

#include "waveglider_sv2_frontseat_driver.pb.h"
#include "waveglider_sv2_frontseat_driver_config.pb.h"
#include "waveglider_sv2_serial_client.h"

extern "C"
{
    FrontSeatInterfaceBase* frontseat_driver_load(iFrontSeatConfig*);
}

class WavegliderSV2FrontSeat : public FrontSeatInterfaceBase
{
  public:
    WavegliderSV2FrontSeat(const iFrontSeatConfig& cfg);

  private: // virtual methods from FrontSeatInterfaceBase
    void loop();

    void send_command_to_frontseat(const goby::moos::protobuf::CommandRequest& command);
    void send_data_to_frontseat(const goby::moos::protobuf::FrontSeatInterfaceData& data);
    void send_raw_to_frontseat(const goby::moos::protobuf::FrontSeatRaw& data);
    goby::moos::protobuf::FrontSeatState frontseat_state() const;
    bool frontseat_providing_data() const;
    void handle_sv2_message(const std::string& message);
    void handle_enumeration_request(const goby::moos::protobuf::SV2RequestEnumerate& msg);
    void handle_request_status(const goby::moos::protobuf::SV2RequestStatus& request);
    void
    handle_request_queued_message(const goby::moos::protobuf::SV2RequestQueuedMessage& request);

    void check_crc(const std::string& message, uint16_t expected);
    void add_crc(std::string* message);
    void encode_and_write(const google::protobuf::Message& message);

  private: // internal non-virtual methods
    void check_connection_state();

  private:
    const WavegliderSV2FrontSeatConfig waveglider_sv2_config_;

    bool frontseat_providing_data_;
    double last_frontseat_data_time_;
    goby::moos::protobuf::FrontSeatState frontseat_state_;

    boost::asio::io_service io_;
    boost::shared_ptr<goby::moos::SV2SerialConnection> serial_;

    boost::circular_buffer<boost::shared_ptr<goby::moos::protobuf::SV2CommandFollowFixedHeading> >
        queued_messages_;

    dccl::Codec dccl_;
};

#endif
