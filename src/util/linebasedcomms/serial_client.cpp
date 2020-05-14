// Copyright 2011-2020:
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

#include <iostream>

#include <thread>

#include "serial_client.h"

goby::util::SerialClient::SerialClient(const std::string& name, unsigned baud,
                                       const std::string& delimiter)
    : LineBasedClient<boost::asio::serial_port>(delimiter),
      serial_port_(io_),
      name_(name),
      baud_(baud)
{
}

bool goby::util::SerialClient::start_specific()
{
    try
    {
        serial_port_.open(name_);
    }
    catch (std::exception& e)
    {
        serial_port_.close();
        return false;
    }

    serial_port_.set_option(boost::asio::serial_port_base::baud_rate(baud_));

    // no flow control
    serial_port_.set_option(boost::asio::serial_port_base::flow_control(
        boost::asio::serial_port_base::flow_control::none));

    // 8N1
    serial_port_.set_option(boost::asio::serial_port_base::character_size(8));
    serial_port_.set_option(
        boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    serial_port_.set_option(
        boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));

    return true;
}
