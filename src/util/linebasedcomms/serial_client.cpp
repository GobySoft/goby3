// Copyright 2009-2020:
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

#include <utility> // for move

#include <boost/asio/serial_port_base.hpp> // for serial_port_base, serial_...

#include "goby/util/asio_compat.h"

#include "serial_client.h"

goby::util::SerialClient::SerialClient(std::string name, unsigned baud,
                                       const std::string& delimiter)
    : LineBasedInterface(delimiter), name_(std::move(name)), baud_(baud)
{
}

goby::util::SerialClient::~SerialClient() { do_close(); }

void goby::util::SerialClient::do_subscribe()
{
    interthread().subscribe_dynamic<goby::middleware::protobuf::SerialStatus>(
        [this](const goby::middleware::protobuf::SerialStatus& status) {
            if (status.index() == this->index())
                status_ = status;
        },
        in_group());
}

void goby::util::SerialClient::do_start()
{
    if (!serial_thread_)
    {
        goby::middleware::protobuf::SerialConfig cfg;
        cfg.set_port(name_);
        cfg.set_baud(baud_);
        cfg.set_end_of_line(delimiter());

        serial_alive_ = true;
        serial_thread_ = std::make_unique<std::thread>([cfg, this]() {
            Thread serial(cfg, this->index());
            auto type_i = std::type_index(typeid(Thread));
            serial.set_type_index(type_i);
            serial.run(serial_alive_);
        });
    }
}

void goby::util::SerialClient::do_close()
{
    if (serial_thread_)
    {
        // wait for the first status message to ensure that shutdown_group_ has been subscribed to
        // only relevant for very fast do_open()/do_close()
        while (!io_thread_ready()) interthread().poll(std::chrono::milliseconds(10));

        auto type_i = std::type_index(typeid(Thread));
        middleware::ThreadIdentifier ti{type_i, index()};
        interthread().publish<Thread::shutdown_group_>(ti);
        serial_thread_->join();
        serial_thread_.reset();
    }
}

void goby::util::SerialClient::send_command(const middleware::protobuf::SerialCommand& command)
{
    interthread().publish_dynamic(command, out_group());
}
