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

#ifndef GOBY_MIDDLEWARE_IO_DETAIL_TCP_SERVER_INTERFACE_H
#define GOBY_MIDDLEWARE_IO_DETAIL_TCP_SERVER_INTERFACE_H

#include <memory>  // for shared_ptr
#include <ostream> // for endl, basic_...
#include <set>     // for set
#include <string>  // for operator<<
#include <utility> // for move

#include <boost/asio/buffer.hpp>       // for buffer
#include <boost/asio/error.hpp>        // for eof, make_er...
#include <boost/asio/ip/tcp.hpp>       // for tcp, tcp::en...
#include <boost/asio/write.hpp>        // for async_write
#include <boost/system/error_code.hpp> // for error_code

#include "goby/exception.h"                             // for Exception
#include "goby/middleware/io/detail/io_interface.h"     // for PubSubLayer
#include "goby/middleware/io/groups.h"                  // for tcp_server_e...
#include "goby/middleware/protobuf/io.pb.h"             // for TCPServerEvent
#include "goby/middleware/protobuf/tcp_config.pb.h"     // for TCPServerConfig
#include "goby/util/debug_logger/flex_ostream.h"        // for glog, FlexOs...
#include "goby/util/debug_logger/logger_manipulators.h" // for group
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
inline bool operator<(const TCPEndPoint& ep_a, const TCPEndPoint& ep_b)
{
    return (ep_a.addr() == ep_b.addr()) ? ep_a.port() < ep_b.port() : ep_a.addr() < ep_b.addr();
}

inline bool operator==(const TCPEndPoint& ep_a, const TCPEndPoint& ep_b)
{
    return (ep_a.addr() == ep_b.addr()) && (ep_a.port() == ep_b.port());
}
} // namespace protobuf
namespace io
{
namespace detail
{
template <typename TCPServerThreadType>
class TCPSession : public std::enable_shared_from_this<TCPSession<TCPServerThreadType>>
{
  public:
    TCPSession(boost::asio::ip::tcp::socket socket, TCPServerThreadType& server)
        : socket_(std::move(socket)),
          server_(server),
          remote_endpoint_(socket_.remote_endpoint()),
          local_endpoint_(socket_.local_endpoint())
    {
    }

    virtual ~TCPSession()
    {
        auto event = std::make_shared<goby::middleware::protobuf::TCPServerEvent>();
        if (server_.index() != -1)
            event->set_index(server_.index());
        event->set_event(goby::middleware::protobuf::TCPServerEvent::EVENT_DISCONNECT);
        *event->mutable_local_endpoint() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);
        *event->mutable_remote_endpoint() =
            endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        event->set_number_of_clients(server_.clients_.size());
        goby::glog.is_debug2() && goby::glog << group(server_.glog_group())
                                             << "Event: " << event->ShortDebugString() << std::endl;
        server_.publish_in(event);
    }

    void start()
    {
        server_.clients_.insert(this->shared_from_this());

        auto event = std::make_shared<goby::middleware::protobuf::TCPServerEvent>();
        if (server_.index() != -1)
            event->set_index(server_.index());
        event->set_event(goby::middleware::protobuf::TCPServerEvent::EVENT_CONNECT);
        *event->mutable_local_endpoint() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);
        *event->mutable_remote_endpoint() =
            endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        event->set_number_of_clients(server_.clients_.size());
        goby::glog.is_debug2() && goby::glog << group(server_.glog_group())
                                             << "Event: " << event->ShortDebugString() << std::endl;
        server_.publish_in(event);
        async_read();
    }

    const boost::asio::ip::tcp::endpoint& remote_endpoint() { return remote_endpoint_; }
    const boost::asio::ip::tcp::endpoint& local_endpoint() { return local_endpoint_; }

    // public so TCPServer can call this
    virtual void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
    {
        auto self(this->shared_from_this());
        boost::asio::async_write(
            socket_, boost::asio::buffer(io_msg->data()),
            [this, self, io_msg](boost::system::error_code ec, std::size_t bytes_transferred) {
                if (!ec)
                {
                    server_.handle_write_success(bytes_transferred);
                }
                else
                {
                    goby::glog.is_warn() && goby::glog << "Write error: " << ec.message()
                                                       << std::endl;
                    server_.clients_.erase(this->shared_from_this());
                }
            });
    }

  protected:
    void handle_read_success(std::size_t bytes_transferred,
                             std::shared_ptr<goby::middleware::protobuf::IOData> io_msg)
    {
        *io_msg->mutable_tcp_src() = endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        *io_msg->mutable_tcp_dest() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);

        server_.handle_read_success(bytes_transferred, io_msg);
    }

    void handle_read_error(const boost::system::error_code& ec)
    {
        if (ec != boost::asio::error::eof)
            goby::glog.is_warn() && goby::glog << "Read error: " << ec.message() << std::endl;
        // erase ourselves from the client list to ensure destruction
        server_.clients_.erase(this->shared_from_this());
    }

    const typename TCPServerThreadType::ConfigType& cfg() { return server_.cfg(); }

    boost::asio::ip::tcp::socket& mutable_socket() { return socket_; }

  private:
    virtual void async_read() = 0;

  private:
    boost::asio::ip::tcp::socket socket_;
    TCPServerThreadType& server_;
    boost::asio::ip::tcp::endpoint remote_endpoint_;
    boost::asio::ip::tcp::endpoint local_endpoint_;
};

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group, PubSubLayer publish_layer,
          PubSubLayer subscribe_layer, typename Config, template <class> class ThreadType,
          bool use_indexed_groups = false>
class TCPServerThread
    : public IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                      boost::asio::ip::tcp::acceptor, ThreadType, use_indexed_groups>
{
    using Base = IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                          boost::asio::ip::tcp::acceptor, ThreadType, use_indexed_groups>;

    using ConfigType = Config;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    TCPServerThread(const Config& config, int index = -1)
        : Base(config, index, std::string("tcp-l: ") + std::to_string(config.bind_port())),
          tcp_socket_(this->mutable_io())
    {
        auto ready = ThreadState::SUBSCRIPTIONS_COMPLETE;
        this->interthread().template publish<line_in_group>(ready);
    }

    ~TCPServerThread() override {}

    template <typename TCPServerThreadType> friend class TCPSession;

  private:
    /// \brief Starts an asynchronous accept on the tcp acceptor
    void async_read() override { async_accept(); }
    void async_accept();

    /// \brief Starts an asynchronous write from data published
    void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override;

    /// \brief Tries to open the tcp server acceptor, and if fails publishes an error
    void open_socket() override { open_acceptor(); }
    void open_acceptor();

    virtual void start_session(boost::asio::ip::tcp::socket tcp_socket) = 0;

  private:
    boost::asio::ip::tcp::endpoint remote_endpoint_;
    boost::asio::ip::tcp::endpoint local_endpoint_;

    boost::asio::ip::tcp::socket tcp_socket_;

    std::set<std::shared_ptr<
        TCPSession<TCPServerThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                   Config, ThreadType, use_indexed_groups>>>>
        clients_;
};
} // namespace detail
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::detail::TCPServerThread<line_in_group, line_out_group, publish_layer,
                                                   subscribe_layer, Config, ThreadType,
                                                   use_indexed_groups>::open_acceptor()
{
    auto& acceptor = this->mutable_socket();
    acceptor.open(boost::asio::ip::tcp::v4());

    if (this->cfg().set_reuseaddr())
    {
        // SO_REUSEADDR
        boost::asio::socket_base::reuse_address option(true);
        acceptor.set_option(option);
    }

    acceptor.bind(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), this->cfg().bind_port()));
    acceptor.listen();

    goby::glog.is_debug2() &&
        goby::glog << group(this->glog_group())
                   << "Successfully bound acceptor to port: " << this->cfg().bind_port()
                   << " and began listening" << std::endl;

    local_endpoint_ = acceptor.local_endpoint();

    auto event = std::make_shared<goby::middleware::protobuf::TCPServerEvent>();
    if (this->index() != -1)
        event->set_index(this->index());
    event->set_event(goby::middleware::protobuf::TCPServerEvent::EVENT_BIND);
    *event->mutable_local_endpoint() = endpoint_convert<protobuf::TCPEndPoint>(local_endpoint_);
    goby::glog.is_debug2() && goby::glog << group(this->glog_group())
                                         << "Event: " << event->ShortDebugString() << std::endl;
    this->publish_in(event);
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::detail::TCPServerThread<line_in_group, line_out_group, publish_layer,
                                                   subscribe_layer, Config, ThreadType,
                                                   use_indexed_groups>::async_accept()
{
    auto& acceptor = this->mutable_socket();
    acceptor.async_accept(tcp_socket_, [this](boost::system::error_code ec) {
        if (!ec)
        {
            goby::glog.is_debug2() && goby::glog << group(this->glog_group())
                                                 << "Received connection from: "
                                                 << tcp_socket_.remote_endpoint() << std::endl;

            start_session(std::move(tcp_socket_));

            this->async_accept();
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
void goby::middleware::io::detail::TCPServerThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer, Config, ThreadType,
    use_indexed_groups>::async_write(std::shared_ptr<const goby::middleware::protobuf::IOData>
                                         io_msg)
{
    if (!io_msg->has_tcp_dest())
        throw(goby::Exception("TCPServerThread requires 'tcp_dest' field to be set in IOData"));
    else if (!io_msg->tcp_dest().all_clients() &&
             (!io_msg->tcp_dest().has_addr() || !io_msg->tcp_dest().has_port()))
        throw(goby::Exception("TCPServerThread requires 'tcp_dest' field to have 'addr'/'port' set "
                              "or all_clients=true in IOData"));

    for (auto& client : clients_)
    {
        if (io_msg->tcp_dest().all_clients() ||
            (io_msg->tcp_dest() ==
             endpoint_convert<protobuf::TCPEndPoint>(client->remote_endpoint())))
        {
            client->async_write(io_msg);
        }
    }
}

#endif
