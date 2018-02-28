// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/acomms/dccl.h"
#include "goby/util/binary.h"
#include "goby/acomms/acomms_helpers.h" // for operator<< of google::protobuf::Message

#include "two_message.pb.h"
#include <exception>
#include <iostream>

using namespace goby;
using goby::acomms::operator<<;

int main()
{
    goby::acomms::DCCLCodec& dccl = *goby::acomms::DCCLCodec::get();

    // validate the Simple protobuf message type as a valid DCCL message type
    dccl.validate<GoToCommand>();
    dccl.validate<VehicleStatus>();

    // show some useful information about all the loaded messages
    std::cout << dccl << std::endl;

    std::cout << "ENCODE / DECODE example:" << std::endl;

    GoToCommand command;
    command.set_destination(2);
    command.set_goto_x(423);
    command.set_goto_y(523);
    command.set_lights_on(true);
    command.set_new_instructions("make_toast");
    command.set_goto_speed(2.3456);
    
    VehicleStatus status;
    status.set_nav_x(234.5);
    status.set_nav_y(451.3);
    status.set_health(VehicleStatus::HEALTH_ABORT);

    std::cout << "passing values to encoder:\n"
              << command << "\n"
              << status << std::endl;

    std::string bytes2, bytes3;
    dccl.encode(&bytes2, command);
    dccl.encode(&bytes3, status);
    
    std::cout << "received hexadecimal string for message 2 (GoToCommand): "
              << goby::util::hex_encode(bytes2)
              << std::endl;

    std::cout << "received hexadecimal string for message 3 (VehicleStatus): "
              << goby::util::hex_encode(bytes3)
              << std::endl;

    command.Clear();
    status.Clear();
    
    std::cout << "passed hexadecimal string for message 2 to decoder: " 
              << goby::util::hex_encode(bytes2)
              << std::endl;
    
    std::cout << "passed hexadecimal string for message 3 to decoder: " 
              << goby::util::hex_encode(bytes3)
              << std::endl;


    dccl.decode(bytes2, &command);
    dccl.decode(bytes3, &status);
    
    std::cout << "received values:\n" 
              << command << "\n"
              << status << std::endl;
    
    return 0;
}

