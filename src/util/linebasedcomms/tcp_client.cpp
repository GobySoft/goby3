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

#include <ostream> // for basic_ostream
#include <utility> // for move

#include "goby/util/asio_compat.h"
#include "goby/util/debug_logger/flex_ostream.h"    // for operator<<, Fle...
#include "goby/util/debug_logger/flex_ostreambuf.h" // for WARN, logger

#include "tcp_client.h"

goby::util::TCPClient::TCPClient(std::string server, unsigned port,
                                 const std::string& delimiter /*= "\r\n"*/,
                                 int retry_interval /*=  10*/)
    : LineBasedInterface(delimiter), server_(std::move(server)), port_(port)
{
    interthread().subscribe_dynamic<goby::middleware::protobuf::TCPClientEvent>(
        [this](const goby::middleware::protobuf::TCPClientEvent& event) {
            if (event.index() == this->index())
            {
                event_ = event;
                if (event.has_local_endpoint())
                    local_endpoint_ = event.local_endpoint();
                if (event.has_remote_endpoint())
                    remote_endpoint_ = event.remote_endpoint();
            }
        },
        in_group());
}

goby::util::TCPClient::~TCPClient() { do_close(); }

void goby::util::TCPClient::do_start()
{
    goby::middleware::protobuf::TCPClientConfig cfg;
    cfg.set_remote_address(server_);
    cfg.set_remote_port(port_);
    cfg.set_end_of_line(delimiter());

    tcp_alive_ = true;
    tcp_thread_ = std::make_unique<std::thread>([cfg, this]() {
        Thread tcp(cfg, this->index());
        auto type_i = std::type_index(typeid(Thread));
        tcp.set_type_index(type_i);
        tcp.run(tcp_alive_);
    });
}

void goby::util::TCPClient::do_close()
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
