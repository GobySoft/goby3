// Copyright 2020-2021:
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

#ifndef GOBY_MIDDLEWARE_IO_LINE_BASED_TCP_SERVER_H
#define GOBY_MIDDLEWARE_IO_LINE_BASED_TCP_SERVER_H

#include <istream> // for istream
#include <memory>  // for make_shared
#include <string>  // for basic_st...
#include <utility> // for move

#include <boost/asio/ip/tcp.hpp>       // for tcp, tcp...
#include <boost/asio/read_until.hpp>   // for async_re...
#include <boost/asio/streambuf.hpp>    // for streambuf
#include <boost/system/error_code.hpp> // for error_code

#include "goby/middleware/io/detail/io_interface.h"         // for PubSubLayer
#include "goby/middleware/io/detail/tcp_server_interface.h" // for TCPServe...
#include "goby/middleware/io/line_based/common.h"           // for match_regex
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
class TCPSessionLineBased : public detail::TCPSession<TCPServerThreadType>
{
  public:
    TCPSessionLineBased(boost::asio::ip::tcp::socket socket, TCPServerThreadType& server)
        : detail::TCPSession<TCPServerThreadType>(std::move(socket), server),
          eol_matcher_(this->cfg().end_of_line())
    {
    }

  private:
    void async_read() override
    {
        auto self(this->shared_from_this());
        boost::asio::async_read_until(
            this->mutable_socket(), buffer_, eol_matcher_,
            [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec && bytes_transferred > 0)
                {
                    auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
                    auto& bytes = *io_msg->mutable_data();
                    bytes = std::string(bytes_transferred, 0);
                    std::istream is(&buffer_);
                    is.read(&bytes[0], bytes_transferred);

                    this->handle_read_success(bytes_transferred, io_msg);
                    async_read();
                }
                else
                {
                    this->handle_read_error(ec);
                }
            });
    }

  private:
    match_regex eol_matcher_;
    boost::asio::streambuf buffer_;
};

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          typename Config = goby::middleware::protobuf::TCPServerConfig,
          template <class> class ThreadType = goby::middleware::SimpleThread,
          bool use_indexed_groups = false>
class TCPServerThreadLineBased
    : public detail::TCPServerThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                     Config, ThreadType, use_indexed_groups>
{
    using Base = detail::TCPServerThread<line_in_group, line_out_group, publish_layer,
                                         subscribe_layer, Config, ThreadType, use_indexed_groups>;

  public:
    TCPServerThreadLineBased(const Config& config, int index = -1) : Base(config, index) {}

  private:
    void start_session(boost::asio::ip::tcp::socket tcp_socket)
    {
        std::make_shared<TCPSessionLineBased<Base>>(std::move(tcp_socket), *this)->start();
    }
};

} // namespace io
} // namespace middleware
} // namespace goby

#endif
