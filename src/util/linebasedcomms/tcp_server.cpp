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

#include "tcp_server.h"

goby::util::TCPServer::TCPServer(unsigned port, const std::string& delimiter)
    : LineBasedInterface(delimiter), port_(port)
{
}

goby::util::TCPServer::~TCPServer() { do_close(); }

void goby::util::TCPServer::do_subscribe()
{
    interthread().subscribe_dynamic<goby::middleware::protobuf::TCPServerEvent>(
        [this](const goby::middleware::protobuf::TCPServerEvent& event) {
            if (event.index() == this->index())
            {
                event_ = event;
                if (event.event() == middleware::protobuf::TCPServerEvent::EVENT_BIND &&
                    event.has_local_endpoint())
                    local_endpoint_ = event.local_endpoint();
                else if (event.event() == middleware::protobuf::TCPServerEvent::EVENT_CONNECT &&
                         event.has_remote_endpoint())
                    remote_endpoints_.insert(event.remote_endpoint());
                else if (event.event() == middleware::protobuf::TCPServerEvent::EVENT_DISCONNECT &&
                         event.has_remote_endpoint())
                    remote_endpoints_.erase(event.remote_endpoint());
            }
        },
        in_group());
}

void goby::util::TCPServer::do_start()
{
    if (!tcp_thread_)
    {
        goby::middleware::protobuf::TCPServerConfig cfg;
        cfg.set_bind_port(port_);
        cfg.set_end_of_line(delimiter());
        cfg.set_set_reuseaddr(true);

        tcp_alive_ = true;
        tcp_thread_ = std::make_unique<std::thread>([cfg, this]() {
            Thread tcp(cfg, this->index());
            auto type_i = std::type_index(typeid(Thread));
            tcp.set_type_index(type_i);
            tcp.run(tcp_alive_);
        });
    }
}

void goby::util::TCPServer::do_close()
{
    if (tcp_thread_)
    {
        while (!io_thread_ready()) interthread().poll(std::chrono::milliseconds(10));

        auto type_i = std::type_index(typeid(Thread));
        middleware::ThreadIdentifier ti{type_i, index()};
        interthread().publish<Thread::shutdown_group_>(ti);
        tcp_thread_->join();
        tcp_thread_.reset();
    }
}
