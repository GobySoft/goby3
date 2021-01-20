// Copyright 2020:
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

#ifndef GOBY_MIDDLEWARE_IO_DETAIL_TCP_CLIENT_INTERFACE_H
#define GOBY_MIDDLEWARE_IO_DETAIL_TCP_CLIENT_INTERFACE_H

#include <memory> // for shared_ptr, __sh...
#include <string> // for string, to_string

#include "goby/middleware/io/detail/io_interface.h" // for PubSubLayer, IOT...
#include "goby/middleware/protobuf/io.pb.h"         // for IOData, TCPEndPo...
#include "goby/middleware/protobuf/tcp_config.pb.h" // for TCPClientConfig
#include <boost/asio/ip/tcp.hpp>                    // for tcp, tcp::endpoint

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
namespace detail
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group, PubSubLayer publish_layer,
          PubSubLayer subscribe_layer, typename Config, template <class> class ThreadType,
          bool use_indexed_groups = false>
class TCPClientThread
    : public IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                      boost::asio::ip::tcp::socket, ThreadType, use_indexed_groups>
{
    using Base = IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                          boost::asio::ip::tcp::socket, ThreadType, use_indexed_groups>;

    using ConfigType = Config;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    TCPClientThread(const Config& config, int index = -1)
        : Base(config, index,
               std::string("tcp: ") + config.remote_address() + ":" +
                   std::to_string(config.remote_port()))
    {
        boost::asio::ip::tcp::resolver resolver(this->mutable_io());
        remote_endpoint_ =
            *resolver.resolve({boost::asio::ip::tcp::v4(), this->cfg().remote_address(),
                               std::to_string(this->cfg().remote_port())});
    }

    ~TCPClientThread()
    {
        auto event = std::make_shared<goby::middleware::protobuf::TCPClientEvent>();
        if (this->index() != -1)
            event->set_index(this->index());
        event->set_event(goby::middleware::protobuf::TCPClientEvent::EVENT_DISCONNECT);
        *event->mutable_local_endpoint() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);
        *event->mutable_remote_endpoint() =
            endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        goby::glog.is_debug2() && goby::glog << group(this->glog_group())
                                             << "Event: " << event->ShortDebugString() << std::endl;
        this->publish_in(event);
    }

  protected:
    void insert_endpoints(std::shared_ptr<goby::middleware::protobuf::IOData>& io_msg)
    {
        *io_msg->mutable_tcp_src() = endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        *io_msg->mutable_tcp_dest() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);
    }

  private:
    void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override
    {
        basic_async_write(this, io_msg);
    }

    /// \brief Tries to open the tcp client socket, and if fails publishes an error
    void open_socket() override
    {
        this->mutable_socket().connect(remote_endpoint_);

        auto event = std::make_shared<goby::middleware::protobuf::TCPClientEvent>();
        if (this->index() != -1)
            event->set_index(this->index());
        event->set_event(goby::middleware::protobuf::TCPClientEvent::EVENT_CONNECT);
        *event->mutable_local_endpoint() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);
        *event->mutable_remote_endpoint() =
            endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        goby::glog.is_debug2() && goby::glog << group(this->glog_group())
                                             << "Event: " << event->ShortDebugString() << std::endl;
        this->publish_in(event);

        local_endpoint_ = this->mutable_socket().local_endpoint();
    }

  private:
    boost::asio::ip::tcp::endpoint remote_endpoint_;
    boost::asio::ip::tcp::endpoint local_endpoint_;
};

} // namespace detail
} // namespace io
} // namespace middleware
} // namespace goby

#endif
