// Copyright 2019-2020:
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

#ifndef UDP_POINT_TO_POINT_20190815H
#define UDP_POINT_TO_POINT_20190815H

#include <iosfwd> // for size_t
#include <memory> // for shared_ptr, __sh...
#include <string> // for to_string

#include <boost/asio/buffer.hpp>       // for buffer
#include <boost/asio/ip/udp.hpp>       // for udp, udp::endpoint
#include <boost/system/error_code.hpp> // for error_code

#include "goby/middleware/io/detail/io_interface.h" // for PubSubLayer, Pub...
#include "goby/middleware/protobuf/io.pb.h"         // for IOData

#include "udp_one_to_many.h"

namespace goby
{
namespace middleware
{
class Group;
}
} // namespace goby
namespace goby
{
namespace middleware
{
namespace protobuf
{
class UDPPointToPointConfig;
}
} // namespace middleware
} // namespace goby

namespace goby
{
namespace middleware
{
namespace io
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD>
class UDPPointToPointThread
    : public UDPOneToManyThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                goby::middleware::protobuf::UDPPointToPointConfig>
{
    using Base = UDPOneToManyThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                    goby::middleware::protobuf::UDPPointToPointConfig>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    UDPPointToPointThread(const goby::middleware::protobuf::UDPPointToPointConfig& config)
        : Base(config)
    {
        boost::asio::ip::udp::resolver resolver(this->mutable_io());
        remote_endpoint_ =
            *resolver.resolve({boost::asio::ip::udp::v4(), this->cfg().remote_address(),
                               std::to_string(this->cfg().remote_port())});
    }

    ~UDPPointToPointThread() {}

  private:
    /// \brief Starts an asynchronous write from data published
    void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override;

  private:
    boost::asio::ip::udp::endpoint remote_endpoint_;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::UDPPointToPointThread<
    line_in_group, line_out_group, publish_layer,
    subscribe_layer>::async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
{
    this->mutable_socket().async_send_to(
        boost::asio::buffer(io_msg->data()), remote_endpoint_,
        [this, io_msg](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                this->handle_write_success(bytes_transferred);
            }
            else
            {
                this->handle_write_error(ec);
            }
        });
}

#endif
