// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "on_demand.pb.h"
#include "goby/acomms/queue.h"
#include "goby/acomms/connect.h"
#include "goby/util/binary.h"

// demonstrates "encode_on_demand" functionality

using goby::acomms::operator<<;

void handle_encode_on_demand(const goby::acomms::protobuf::ModemTransmission& request_msg,
                             google::protobuf::Message* data_msg);

void handle_receive(const google::protobuf::Message &msg);

int main(int argc, char* argv[])
{    
    // get a pointer to the QueueManager singleton
    goby::acomms::QueueManager q_manager;

    // configure the QueueManager with our modem id
    goby::acomms::protobuf::QueueManagerConfig cfg;
    const int MY_MODEM_ID = 1;
    cfg.set_modem_id(MY_MODEM_ID);
    q_manager.set_cfg(cfg);
    
    // add our queue
    q_manager.add_queue<GobyMessage>();

    // take a look at what the QueueManager contains
    std::cout << q_manager << std::endl;

    // connect the encode on demand slot
    goby::acomms::connect(&q_manager.signal_data_on_demand, &handle_encode_on_demand);
    // connect the receive slot
    goby::acomms::connect(&q_manager.signal_receive, &handle_receive);


    // make a request for a 32 byte message
    goby::acomms::protobuf::ModemTransmission request_msg;
    request_msg.set_max_frame_bytes(32);
    request_msg.set_max_num_frames(1);

    std::cout << "requesting data..." << std::endl;
    std::cout << "request message:" << request_msg << std::endl;

    q_manager.handle_modem_data_request(&request_msg);


    std::cout << "data message: " << request_msg << std::endl;
    std::cout << "\tdata as hex: " << goby::util::hex_encode(request_msg.frame(0)) << std::endl;

    std::cout << "feeding the requested data back to the QueueManager as incoming... " << std::endl;
    q_manager.handle_modem_receive(request_msg);
}


void handle_encode_on_demand(const goby::acomms::protobuf::ModemTransmission& request_msg,
                             google::protobuf::Message* data_msg)
{
    GobyMessage msg;

    static int i = 0;
    msg.set_a(i++);
    msg.set_b(i++);
    
    std::cout << "encoded on demand: " << msg << std::endl;
    
    // put our message into the data_msg for return
    data_msg->CopyFrom(msg);
}



void handle_receive(const google::protobuf::Message& in_msg)
{
    GobyMessage msg;
    msg.CopyFrom(in_msg);
    
    std::cout << "Received: " << msg << std::endl;
}

