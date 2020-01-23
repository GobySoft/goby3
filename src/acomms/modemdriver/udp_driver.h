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

#ifndef UDPModemDriver20120409H
#define UDPModemDriver20120409H

#include "goby/time.h"

#include "goby/acomms/modemdriver/driver_base.h"
#include "goby/acomms/protobuf/udp_driver.pb.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

namespace goby
{
namespace acomms
{
class UDPDriver : public ModemDriverBase
{
  public:
    UDPDriver();
    ~UDPDriver();

    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;

  private:
    void start_send(const protobuf::ModemTransmission& msg);
    void send_complete(const boost::system::error_code& error, std::size_t bytes_transferred);
    void start_receive();
    void receive_complete(const boost::system::error_code& error, std::size_t bytes_transferred);
    void receive_message(const protobuf::ModemTransmission& m);

  private:
    protobuf::DriverConfig driver_cfg_;
    boost::asio::io_service io_service_;
    std::unique_ptr<boost::asio::ip::udp::socket> socket_;
    // modem id to endpoint
    std::multimap<int, boost::asio::ip::udp::endpoint> receivers_;
    boost::asio::ip::udp::endpoint sender_;

    // (16 bit length = 65535 - 8 byte UDP header - 20 byte IP
    static constexpr size_t UDP_MAX_PACKET_SIZE = 65507;

    std::array<char, UDP_MAX_PACKET_SIZE> receive_buffer_;

    // ids we are providing acks for, normally just our modem_id()
    std::set<unsigned> application_ack_ids_;

    std::uint32_t next_frame_{0};
};
} // namespace acomms
} // namespace goby
#endif
