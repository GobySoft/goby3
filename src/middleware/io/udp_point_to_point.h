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

#ifndef UDP_POINT_TO_POINT_20190815H
#define UDP_POINT_TO_POINT_20190815H

#include <boost/asio/ip/udp.hpp>

#include "goby/middleware/io/common.h"
#include "goby/middleware/protobuf/udp_config.pb.h"

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
    : public IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                      goby::middleware::protobuf::UDPPointToPointConfig,
                      boost::asio::ip::udp::socket>
{
    using Base =
        IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                 goby::middleware::protobuf::UDPPointToPointConfig, boost::asio::ip::udp::socket>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    UDPPointToPointThread(const goby::middleware::protobuf::UDPPointToPointConfig& config)
        : Base(config)
    {
    }

    ~UDPPointToPointThread() {}

  protected:
    /// \brief Starts an asynchronous read on the udp socket.
    virtual void async_read() override;

    /// \brief Starts an asynchronous write from data published
    virtual void async_write(const std::string& bytes) override;

  private:
    /// \brief Tries to open the udp socket, and if fails publishes an error
    void open_socket() override;

  private:
    static constexpr int max_udp_size{65507};
    std::array<char, max_udp_size> rx_message_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    boost::asio::ip::udp::endpoint sender_endpoint_;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::UDPPointToPointThread<line_in_group, line_out_group, publish_layer,
                                                 subscribe_layer>::open_socket()
{
    this->mutable_socket().open(boost::asio::ip::udp::v4());

    if (this->cfg().remote_port() == 0)
    {
        // SO_REUSEADDR
        boost::asio::socket_base::reuse_address option(true);
        this->mutable_socket().set_option(option);
    }
    else if (this->cfg().bind_port() == 0)
    {
        // SO_BROADCAST
        this->mutable_socket().set_option(boost::asio::socket_base::broadcast(true));
    }

    this->mutable_socket().bind(
        boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), this->cfg().bind_port()));

    boost::asio::ip::udp::resolver resolver(this->mutable_io());
    remote_endpoint_ = *resolver.resolve({boost::asio::ip::udp::v4(), this->cfg().remote_address(),
                                          std::to_string(this->cfg().remote_port())});
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::UDPPointToPointThread<line_in_group, line_out_group, publish_layer,
                                                 subscribe_layer>::async_read()
{
    this->mutable_socket().async_receive_from(
        boost::asio::buffer(rx_message_), sender_endpoint_,
        [this](const boost::system::error_code& ec, size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                this->handle_read_success(
                    bytes_transferred,
                    std::string(rx_message_.begin(), rx_message_.begin() + bytes_transferred));
                this->async_read();
            }
            else
            {
                this->handle_read_error(ec);
            }
        });
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::UDPPointToPointThread<line_in_group, line_out_group, publish_layer,
                                                 subscribe_layer>::async_write(const std::string&
                                                                                   bytes)
{
    this->mutable_socket().async_send_to(
        boost::asio::buffer(bytes), remote_endpoint_,
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
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
