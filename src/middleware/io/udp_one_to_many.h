// Copyright 2019-2023:
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

#ifndef GOBY_MIDDLEWARE_IO_UDP_ONE_TO_MANY_H
#define GOBY_MIDDLEWARE_IO_UDP_ONE_TO_MANY_H

#include <array>                 // for array
#include <boost/asio/buffer.hpp> // for buffer
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/udp.hpp>       // for udp, udp::endpoint
#include <boost/asio/socket_base.hpp>  // for socket_base
#include <boost/system/error_code.hpp> // for error_code
#include <cstddef>                     // for size_t
#include <memory>                      // for shared_ptr, __s...
#include <string>                      // for string, to_string

#include "goby/exception.h"                         // for Exception
#include "goby/middleware/io/detail/io_interface.h" // for PubSubLayer
#include "goby/middleware/protobuf/io.pb.h"         // for IOData, UDPEndP...
#include "goby/middleware/protobuf/udp_config.pb.h" // for UDPOneToManyConfig

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
namespace io
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          typename Config = goby::middleware::protobuf::UDPOneToManyConfig,
          template <class> class ThreadType = goby::middleware::SimpleThread,
          bool use_indexed_groups = false>
class UDPOneToManyThread
    : public detail::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                              boost::asio::ip::udp::socket, ThreadType, use_indexed_groups>
{
    using Base =
        detail::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                         boost::asio::ip::udp::socket, ThreadType, use_indexed_groups>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    UDPOneToManyThread(const Config& config, int index = -1, bool is_final = true)
        : Base(config, index, std::string("udp: ") + std::to_string(config.bind_port()))
    {
        if (is_final)
        {
            auto ready = ThreadState::SUBSCRIPTIONS_COMPLETE;
            this->interthread().template publish<line_in_group>(ready);
        }
    }

    ~UDPOneToManyThread() override {}

  protected:
    /// \brief Starts an asynchronous read on the udp socket.
    virtual void async_read() override;

    /// \brief Starts an asynchronous write from data published
    virtual void
    async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override;

  private:
    /// \brief Tries to open the udp socket, and if fails publishes an error
    void open_socket() override;

  private:
    static constexpr int max_udp_size{65507};
    std::array<char, max_udp_size> rx_message_;
    boost::asio::ip::udp::endpoint sender_endpoint_;
    boost::asio::ip::udp::endpoint local_endpoint_;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::UDPOneToManyThread<line_in_group, line_out_group, publish_layer,
                                              subscribe_layer, Config, ThreadType,
                                              use_indexed_groups>::open_socket()
{
    auto protocol = this->cfg().ipv6() ? boost::asio::ip::udp::v6() : boost::asio::ip::udp::v4();
    this->mutable_socket().open(protocol);

    if (this->cfg().set_reuseaddr())
    {
        // SO_REUSEADDR
        boost::asio::socket_base::reuse_address option(true);
        this->mutable_socket().set_option(option);
    }

    if (this->cfg().set_broadcast())
    {
        // SO_BROADCAST
        this->mutable_socket().set_option(boost::asio::socket_base::broadcast(true));
    }

    this->mutable_socket().bind(boost::asio::ip::udp::endpoint(protocol, this->cfg().bind_port()));
    local_endpoint_ = this->mutable_socket().local_endpoint();
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::UDPOneToManyThread<line_in_group, line_out_group, publish_layer,
                                              subscribe_layer, Config, ThreadType,
                                              use_indexed_groups>::async_read()
{
    this->mutable_socket().async_receive_from(
        boost::asio::buffer(rx_message_), sender_endpoint_,
        [this](const boost::system::error_code& ec, size_t bytes_transferred)
        {
            if (!ec && bytes_transferred > 0)
            {
                auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
                *io_msg->mutable_data() =
                    std::string(rx_message_.begin(), rx_message_.begin() + bytes_transferred);

                *io_msg->mutable_udp_src() =
                    detail::endpoint_convert<protobuf::UDPEndPoint>(sender_endpoint_);
                *io_msg->mutable_udp_dest() =
                    detail::endpoint_convert<protobuf::UDPEndPoint>(local_endpoint_);

                this->handle_read_success(bytes_transferred, io_msg);
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
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::UDPOneToManyThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer, Config, ThreadType,
    use_indexed_groups>::async_write(std::shared_ptr<const goby::middleware::protobuf::IOData>
                                         io_msg)
{
    if (!io_msg->has_udp_dest())
        throw(goby::Exception("UDPOneToManyThread requires 'udp_dest' field to be set in IOData"));

    boost::asio::ip::udp::resolver resolver(this->mutable_io());
    boost::asio::ip::udp::endpoint remote_endpoint =
        *resolver.resolve({io_msg->udp_dest().addr(), std::to_string(io_msg->udp_dest().port()),
                           boost::asio::ip::resolver_query_base::numeric_service});

    this->mutable_socket().async_send_to(
        boost::asio::buffer(io_msg->data()), remote_endpoint,
        [this, io_msg](const boost::system::error_code& ec, std::size_t bytes_transferred)
        {
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
