// Copyright 2015-2021:
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_IRIDIUM_SHORE_RUDICS_H
#define GOBY_ACOMMS_MODEMDRIVER_IRIDIUM_SHORE_RUDICS_H

#include "goby/util/asio_compat.h"
#include <boost/asio.hpp>

#include <boost/bind.hpp>
#include <boost/signals2.hpp>

#include "goby/time.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"

namespace goby
{
namespace acomms
{
class RUDICSConnection : public std::enable_shared_from_this<RUDICSConnection>
{
  public:
    static std::shared_ptr<RUDICSConnection> create(
#ifdef USE_BOOST_IO_SERVICE
        boost::asio::io_service& executor)
#else
        const boost::asio::executor& executor)
#endif
    {
        return std::shared_ptr<RUDICSConnection>(new RUDICSConnection(executor));
    }

    boost::asio::ip::tcp::socket& socket() { return socket_; }

    void start()
    {
        remote_endpoint_str_ = boost::lexical_cast<std::string>(socket_.remote_endpoint());
        read_start();
    }

    void close()
    {
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        socket_.close();
    }

    void read_start()
    {
        boost::asio::async_read_until(socket_, buffer_, '\r',
                                      boost::bind(&RUDICSConnection::handle_read, this, _1, _2));
    }

    void write_start(const std::string& data)
    {
        boost::asio::async_write(socket_, boost::asio::buffer(data),
                                 boost::bind(&RUDICSConnection::handle_write, this, _1, _2));
    }

    ~RUDICSConnection()
    {
        using goby::glog;
        using goby::util::logger::DEBUG1;
        glog.is(DEBUG1) && glog << "Disconnecting from: " << remote_endpoint_str_ << std::endl;
    }

    void add_packet_failure()
    {
        using goby::glog;
        using goby::util::logger::DEBUG1;
        const int max_packet_failures = 3;
        if (++packet_failures_ >= max_packet_failures)
        {
            glog.is(DEBUG1) && glog << "More than " << max_packet_failures << " bad RUDICS packets."
                                    << std::endl;
            close();
        }
    }

    boost::signals2::signal<void(const std::string& line,
                                 std::shared_ptr<RUDICSConnection> connection)>
        line_signal;
    boost::signals2::signal<void(std::shared_ptr<RUDICSConnection> connection)> disconnect_signal;

    const std::string& remote_endpoint_str() { return remote_endpoint_str_; }

  private:
    RUDICSConnection(
#ifdef USE_BOOST_IO_SERVICE
        boost::asio::io_service& executor)
#else
        const boost::asio::executor& executor)
#endif
        : socket_(executor), remote_endpoint_str_("Unknown"), packet_failures_(0)
    {
    }

    void handle_write(const boost::system::error_code& error, size_t bytes_transferred)
    {
        if (error)
        {
            using goby::glog;
            using goby::util::logger::WARN;
            glog.is(WARN) && glog << "Error writing to TCP connection: " << error << std::endl;
            disconnect_signal(shared_from_this());
        }
    }

    void handle_read(const boost::system::error_code& error, size_t bytes_transferred)
    {
        using goby::glog;
        using goby::util::logger::DEBUG1;
        using goby::util::logger::WARN;
        if (!error)
        {
            std::istream istrm(&buffer_);
            std::string line;
            std::getline(istrm, line, '\r');
            line_signal(line + "\r", shared_from_this());
            read_start();
        }
        else
        {
            if (error == boost::asio::error::eof)
            {
                glog.is(DEBUG1) && glog << "Connection reached EOF" << std::endl;
            }
            else if (error == boost::asio::error::operation_aborted)
            {
                glog.is(DEBUG1) && glog << "Read operation aborted (socket closed)" << std::endl;
            }
            else
            {
                glog.is(WARN) && glog << "Error reading from TCP connection: " << error
                                      << std::endl;
            }

            disconnect_signal(shared_from_this());
        }
    }

  private:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf buffer_;
    std::string remote_endpoint_str_;
    int packet_failures_;
};

class RUDICSServer
{
  public:
    RUDICSServer(boost::asio::io_context& io_context, int port)
        : acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
        start_accept();
    }

    std::set<std::shared_ptr<RUDICSConnection> >& connections() { return connections_; }

    boost::signals2::signal<void(std::shared_ptr<RUDICSConnection> connection)> connect_signal;

    void disconnect(std::shared_ptr<RUDICSConnection> connection) { connection->close(); }

  private:
    void start_accept()
    {
        std::shared_ptr<RUDICSConnection> new_connection =
#ifdef USE_BOOST_IO_SERVICE
            RUDICSConnection::create(acceptor_.get_io_service());
#else
            RUDICSConnection::create(acceptor_.get_executor());
#endif
        acceptor_.async_accept(new_connection->socket(),
                               boost::bind(&RUDICSServer::handle_accept, this, new_connection,
                                           boost::asio::placeholders::error));
    }

    void handle_accept(std::shared_ptr<RUDICSConnection> new_connection,
                       const boost::system::error_code& error)
    {
        if (!error)
        {
            using namespace goby::util::logger;
            using goby::glog;

            connections_.insert(new_connection);

            new_connection->disconnect_signal.connect(
                boost::bind(&RUDICSServer::handle_disconnect, this, _1));
            connect_signal(new_connection);
            new_connection->start();
            glog.is(DEBUG1) && glog << "Received connection from: "
                                    << new_connection->remote_endpoint_str() << std::endl;
        }

        start_accept();
    }

    void handle_disconnect(std::shared_ptr<RUDICSConnection> connection)
    {
        using goby::glog;
        using goby::util::logger::DEBUG1;

        connections_.erase(connection);

        glog.is(DEBUG1) &&
            glog << "Server removing connection: " << connection->remote_endpoint_str()
                 << ". Remaining connection count: " << connections_.size() << std::endl;
    }

    std::set<std::shared_ptr<RUDICSConnection> > connections_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

} // namespace acomms
} // namespace goby

#endif
