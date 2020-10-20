// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef UDPMulticastModemDriver20190620H
#define UDPMulticastModemDriver20190620H

#include "goby/time.h"

#include "goby/acomms/modemdriver/driver_base.h"
#include "goby/acomms/protobuf/udp_multicast_driver.pb.h"

#include "goby/util/asio-compat.h"
#include <boost/asio.hpp>

namespace goby
{
namespace acomms
{
class UDPMulticastDriver : public ModemDriverBase
{
  public:
    UDPMulticastDriver();
    ~UDPMulticastDriver();

    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;

  private:
    void start_send(const google::protobuf::Message& msg);
    void send_complete(const boost::system::error_code& error, std::size_t bytes_transferred);
    void start_receive();
    void receive_complete(const boost::system::error_code& error, std::size_t bytes_transferred);
    void receive_message(const protobuf::ModemTransmission& m);

    const udp_multicast::protobuf::Config& multicast_driver_cfg() const
    {
        return driver_cfg_.GetExtension(udp_multicast::protobuf::config);
    }

  private:
    protobuf::DriverConfig driver_cfg_;
    boost::asio::io_context io_context_;
    boost::asio::ip::udp::socket socket_{io_context_};
    boost::asio::ip::udp::endpoint receiver_;
    boost::asio::ip::udp::endpoint sender_;

    // (16 bit length = 65535 - 8 byte UDP header - 20 byte IP
    static constexpr size_t UDP_MAX_PACKET_SIZE = 65507;

    std::array<char, UDP_MAX_PACKET_SIZE> receive_buffer_;
    std::uint32_t next_frame_{0};

    std::map<int, int> rate_to_bytes_;
};
} // namespace acomms
} // namespace goby
#endif
