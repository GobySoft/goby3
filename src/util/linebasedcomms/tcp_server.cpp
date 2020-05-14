// Copyright 2011-2019:
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

#include "tcp_server.h"

std::shared_ptr<goby::util::TCPConnection>
goby::util::TCPConnection::create(LineBasedInterface* interface)
{
    return std::shared_ptr<TCPConnection>(new TCPConnection(interface));
}

void goby::util::TCPConnection::socket_write(const protobuf::Datagram& line) // give it a string
{
    bool write_in_progress = !out().empty(); // is there anything currently being written?
    out().push_back(line);                   // store in write buffer
    if (!write_in_progress)                  // if nothing is currently being written, then start
        write_start();
}

void goby::util::TCPConnection::socket_close(const boost::system::error_code& error)
{ // something has gone wrong, so close the socket & make this object inactive
    if (error ==
        boost::asio::error::operation_aborted) // if this call is thex result of a timer cancel()
        return; // ignore it because the connection cancelled the timer

    if (error)
        socket_.close();
}

void goby::util::TCPServer::do_write(const protobuf::Datagram& line)
{
    if (line.has_dest())
    {
        std::map<Endpoint, std::shared_ptr<TCPConnection> >::iterator it =
            connections_.find(line.dest());
        if (it != connections_.end())
            (it->second)->write(line);
    }
    else
    {
        for (std::map<Endpoint, std::shared_ptr<TCPConnection> >::iterator
                 it = connections_.begin(),
                 end = connections_.end();
             it != end; ++it)
            (it->second)->write(line);
    }
}

// close all the connections, it's up to the clients to try to reconnect
void goby::util::TCPServer::do_close(const boost::system::error_code& error,
                                     goby::util::TCPServer::Endpoint endpt /* = ""*/)
{
    if (!endpt.empty())
    {
        std::map<Endpoint, std::shared_ptr<TCPConnection> >::iterator it = connections_.find(endpt);
        if (it != connections_.end())
            (it->second)->close(error);
    }
    else
    {
        for (std::map<Endpoint, std::shared_ptr<TCPConnection> >::iterator
                 it = connections_.begin(),
                 end = connections_.end();
             it != end; ++it)
            (it->second)->close(error);
    }
}

const std::map<goby::util::TCPServer::Endpoint, std::shared_ptr<goby::util::TCPConnection> >&
goby::util::TCPServer::connections()
{
    typedef std::map<Endpoint, std::shared_ptr<TCPConnection> >::iterator It;
    It it = connections_.begin(), it_end = connections_.end();
    while (it != it_end)
    {
        if (!(it->second)->socket().is_open())
        {
            It to_delete = it;
            ++it; // increment before erasing!
            connections_.erase(to_delete);
        }
        else
        {
            ++it;
        }
    }
    return connections_;
}

void goby::util::TCPServer::start_accept()
{
    new_connection_ = TCPConnection::create(this);
    acceptor_.async_accept(new_connection_->socket(),
                           boost::bind(&TCPServer::handle_accept, this, new_connection_,
                                       boost::asio::placeholders::error));
}

void goby::util::TCPServer::handle_accept(std::shared_ptr<TCPConnection> new_connection,
                                          const boost::system::error_code& error)
{
    if (!error)
    {
        new_connection_->start();
        connections_.insert(std::make_pair(new_connection_->remote_endpoint(), new_connection_));
        start_accept();
    }
}
