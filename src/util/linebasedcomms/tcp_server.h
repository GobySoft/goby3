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

#ifndef TCPServer20100719H
#define TCPServer20100719H

#include <deque>
#include <iostream>
#include <set>
#include <string>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>

#include "goby/util/as.h"

#include "connection.h"
#include "interface.h"

namespace goby
{
namespace util
{
class TCPConnection;

/// provides a basic TCP server for line by line text based communications to a one or more remote TCP clients
class TCPServer : public LineBasedInterface
{
  public:
    /// \brief create a TCP server
    ///
    /// \param port port of the server (use 50000+ to avoid problems with special system ports)
    /// \param delimiter string used to split lines
    TCPServer(unsigned port, const std::string& delimiter = "\r\n")
        : LineBasedInterface(delimiter),
          acceptor_(io_service(), boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
    }
    virtual ~TCPServer() {}

    typedef std::string Endpoint;
    void close(const Endpoint& endpoint)
    {
        io_service_.post(
            boost::bind(&TCPServer::do_close, this, boost::system::error_code(), endpoint));
    }

    /// \brief string representation of the local endpoint (e.g. 192.168.1.105:54230
    std::string local_endpoint() { return goby::util::as<std::string>(acceptor_.local_endpoint()); }

    const std::map<Endpoint, boost::shared_ptr<TCPConnection> >& connections();

    friend class TCPConnection;
    friend class LineBasedConnection<boost::asio::ip::tcp::socket>;

  private:
    void do_start()
    {
        start_accept();
        set_active(true);
    }

    void do_write(const protobuf::Datagram& line);
    void do_close(const boost::system::error_code& error) { do_close(error, ""); }
    void do_close(const boost::system::error_code& error, Endpoint endpt);

  private:
    void start_accept();
    void handle_accept(boost::shared_ptr<TCPConnection> new_connection,
                       const boost::system::error_code& error);

  private:
    std::string server_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::shared_ptr<TCPConnection> new_connection_;
    std::map<Endpoint, boost::shared_ptr<TCPConnection> > connections_;
};

class TCPConnection : public boost::enable_shared_from_this<TCPConnection>,
                      public LineBasedConnection<boost::asio::ip::tcp::socket>
{
  public:
    static boost::shared_ptr<TCPConnection> create(LineBasedInterface* interface);

    boost::asio::ip::tcp::socket& socket() { return socket_; }

    void start() { socket_.get_io_service().post(boost::bind(&TCPConnection::read_start, this)); }

    void write(const protobuf::Datagram& msg)
    {
        socket_.get_io_service().post(boost::bind(&TCPConnection::socket_write, this, msg));
    }

    void close(const boost::system::error_code& error)
    {
        socket_.get_io_service().post(boost::bind(&TCPConnection::socket_close, this, error));
    }

    std::string local_endpoint() { return goby::util::as<std::string>(socket_.local_endpoint()); }
    std::string remote_endpoint() { return goby::util::as<std::string>(socket_.remote_endpoint()); }

  private:
    void socket_write(const protobuf::Datagram& line);
    void socket_close(const boost::system::error_code& error);

    TCPConnection(LineBasedInterface* interface)
        : LineBasedConnection<boost::asio::ip::tcp::socket>(interface),
          socket_(interface->io_service())
    {
    }

  private:
    boost::asio::ip::tcp::socket socket_;
};
} // namespace util

} // namespace goby

#endif
