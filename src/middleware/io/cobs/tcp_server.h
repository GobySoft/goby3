// Copyright 2020-2022:
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

#ifndef GOBY_MIDDLEWARE_IO_COBS_TCP_SERVER_H
#define GOBY_MIDDLEWARE_IO_COBS_TCP_SERVER_H

#include <istream> // for istream
#include <memory>  // for make_shared
#include <string>  // for basic_st...
#include <utility> // for move

#include <boost/asio/ip/tcp.hpp>       // for tcp, tcp...
#include <boost/asio/read_until.hpp>   // for async_re...
#include <boost/asio/streambuf.hpp>    // for streambuf
#include <boost/system/error_code.hpp> // for error_code

#include "goby/middleware/io/cobs/common.h"
#include "goby/middleware/io/detail/io_interface.h"         // for PubSubLayer
#include "goby/middleware/io/detail/tcp_server_interface.h" // for TCPServe...
#include "goby/middleware/protobuf/io.pb.h"                 // for IOData
#include "goby/middleware/protobuf/tcp_config.pb.h"         // for TCPServe...

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
template <typename TCPServerThreadType>
class TCPSessionCOBS : public detail::TCPSession<TCPServerThreadType>
{
  public:
    TCPSessionCOBS(boost::asio::ip::tcp::socket socket, TCPServerThreadType& server)
        : detail::TCPSession<TCPServerThreadType>(std::move(socket), server)
    {
    }

    template <class Thread>
    friend void cobs_async_write(Thread* this_thread,
                                 std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg);

    template <class Thread, class ThreadBase>
    friend void cobs_async_read(Thread* this_thread, std::shared_ptr<ThreadBase> self);

  private:
    void async_read() override
    {
        auto self(this->shared_from_this());
        cobs_async_read(this, self);
    }

    void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override
    {
        cobs_async_write(this, io_msg);
    }

  private:
    boost::asio::streambuf buffer_;
};

template <const goby::middleware::Group& packet_in_group,
          const goby::middleware::Group& packet_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          typename Config = goby::middleware::protobuf::TCPServerConfig,
          template <class> class ThreadType = goby::middleware::SimpleThread,
          bool use_indexed_groups = false>
class TCPServerThreadCOBS
    : public detail::TCPServerThread<packet_in_group, packet_out_group, publish_layer,
                                     subscribe_layer, Config, ThreadType, use_indexed_groups>
{
    using Base = detail::TCPServerThread<packet_in_group, packet_out_group, publish_layer,
                                         subscribe_layer, Config, ThreadType, use_indexed_groups>;

  public:
    TCPServerThreadCOBS(const Config& config, int index = -1) : Base(config, index) {}

  private:
    void start_session(boost::asio::ip::tcp::socket tcp_socket)
    {
        std::make_shared<TCPSessionCOBS<Base>>(std::move(tcp_socket), *this)->start();
    }
};

} // namespace io
} // namespace middleware
} // namespace goby

#endif
