// Copyright 2010-2021:
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

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "goby/middleware/io/line_based/tcp_client.h"
#include "goby/middleware/protobuf/io.pb.h"

#include "interface.h"

namespace goby
{
namespace util
{
/// provides a basic TCP client for line by line text based communications to a remote TCP server
class TCPClient : public LineBasedInterface
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
    ~TCPClient() override;

    /// \brief string representation of the local endpoint (e.g. 192.168.1.105:54230)
    std::string local_endpoint() override
    {
        return local_endpoint_.addr() + ":" + std::to_string(local_endpoint_.port());
    }
    /// \brief string representation of the remote endpoint, (e.g. 192.168.1.106:50000)
    std::string remote_endpoint() override
    {
        return remote_endpoint_.addr() + ":" + std::to_string(remote_endpoint_.port());
    }

  private:
    void do_subscribe() override;
    void do_start() override;
    void do_close() override;

  private:
    std::string server_;
    unsigned port_;

    using Thread = goby::middleware::io::TCPClientThreadLineBased<
        groups::linebasedcomms_in, groups::linebasedcomms_out,
        goby::middleware::io::PubSubLayer::INTERTHREAD,
        goby::middleware::io::PubSubLayer::INTERTHREAD, goby::middleware::protobuf::TCPClientConfig,
        goby::util::LineBasedCommsThreadStub, true>;

    std::atomic<bool> tcp_alive_{false};
    std::unique_ptr<std::thread> tcp_thread_;

    goby::middleware::protobuf::TCPClientEvent event_;

    goby::middleware::protobuf::TCPEndPoint remote_endpoint_;
    goby::middleware::protobuf::TCPEndPoint local_endpoint_;
};
} // namespace util
} // namespace goby

#endif
