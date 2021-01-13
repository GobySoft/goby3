// Copyright 2010-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef GOBY_UTIL_LINEBASEDCOMMS_TCP_CLIENT_H
#define GOBY_UTIL_LINEBASEDCOMMS_TCP_CLIENT_H

#include "client_base.h"
#include "goby/util/as.h"

namespace goby
{
namespace util
{
/// provides a basic TCP client for line by line text based communications to a remote TCP server
class TCPClient : public LineBasedClient<boost::asio::ip::tcp::socket>
{
  public:
    /// \brief create a TCPClient
    ///
    /// \param server domain name or IP address of the remote server
    /// \param port port of the remote server
    /// \param delimiter string used to split lines
    /// \param retry_interval Time between reconnects in seconds
    TCPClient(std::string server, unsigned port, const std::string& delimiter = "\r\n",
              int retry_interval = 10);

    boost::asio::ip::tcp::socket& socket() override { return socket_; }

    /// \brief string representation of the local endpoint (e.g. 192.168.1.105:54230
    std::string local_endpoint() override
    {
        return goby::util::as<std::string>(socket_.local_endpoint());
    }
    /// \brief string representation of the remote endpoint, (e.g. 192.168.1.106:50000
    std::string remote_endpoint() override
    {
        return goby::util::as<std::string>(socket_.remote_endpoint());
    }

  private:
    bool start_specific() override;

    friend class TCPConnection;
    friend class LineBasedConnection<boost::asio::ip::tcp::socket>;

  private:
    boost::asio::ip::tcp::socket socket_;
    std::string server_;
    std::string port_;
};
} // namespace util
} // namespace goby

#endif
