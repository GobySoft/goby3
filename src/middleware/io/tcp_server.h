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

#pragma once

#include <boost/asio/ip/tcp.hpp>

#include "goby/middleware/io/common.h"
#include "goby/middleware/protobuf/tcp_config.pb.h"

#include "line_based.h"

namespace goby
{
namespace middleware
{
namespace io
{
bool operator==(const protobuf::TCPEndPoint& ep_a, const protobuf::TCPEndPoint& ep_b)
{
    return (ep_a.addr() == ep_b.addr()) && (ep_a.port() == ep_b.port());
}

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
        goby::middleware::protobuf::TCPServerEvent event;
        event.set_event(goby::middleware::protobuf::TCPServerEvent::EVENT_DISCONNECT);
        *event.mutable_client_endpoint() =
            endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        event.set_number_of_clients(server_.clients_.size());
        server_.interthread().template publish<groups::tcp_server_event>(event);
    }

    void start()
    {
        server_.clients_.insert(this->shared_from_this());

        goby::middleware::protobuf::TCPServerEvent event;
        event.set_event(goby::middleware::protobuf::TCPServerEvent::EVENT_CONNECT);
        *event.mutable_client_endpoint() =
            endpoint_convert<protobuf::TCPEndPoint>(remote_endpoint_);
        event.set_number_of_clients(server_.clients_.size());

        server_.interthread().template publish<groups::tcp_server_event>(event);
        async_read();
    }

    const boost::asio::ip::tcp::endpoint& remote_endpoint() { return remote_endpoint_; }
    const boost::asio::ip::tcp::endpoint& local_endpoint() { return local_endpoint_; }

    // public so TCPServer can call this
    virtual void async_write(const std::string& bytes)
    {
        auto self(this->shared_from_this());
        boost::asio::async_write(
            socket_, boost::asio::buffer(bytes),
            [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
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
          const goby::middleware::Group& line_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          typename Config = goby::middleware::protobuf::TCPServerConfig>
class TCPServerThread : public IOThread<line_in_group, line_out_group, publish_layer,
                                        subscribe_layer, Config, boost::asio::ip::tcp::acceptor>
{
    using Base = IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config,
                          boost::asio::ip::tcp::acceptor>;

    using ConfigType = Config;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    TCPServerThread(const Config& config)
        : Base(config, -1, std::string("tcp-l: ") + std::to_string(config.bind_port())),
          tcp_socket_(this->mutable_io())
    {
    }

    virtual ~TCPServerThread() {}

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

    std::set<std::shared_ptr<TCPSession<
        TCPServerThread<line_in_group, line_out_group, publish_layer, subscribe_layer, Config>>>>
        clients_;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config>
void goby::middleware::io::TCPServerThread<line_in_group, line_out_group, publish_layer,
                                           subscribe_layer, Config>::open_acceptor()
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
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config>
void goby::middleware::io::TCPServerThread<line_in_group, line_out_group, publish_layer,
                                           subscribe_layer, Config>::async_accept()
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
          goby::middleware::io::PubSubLayer subscribe_layer, typename Config>
void goby::middleware::io::TCPServerThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer,
    Config>::async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
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
            client->async_write(io_msg->data());
        }
    }
}
