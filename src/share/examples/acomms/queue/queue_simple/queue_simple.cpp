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

// queues a single message from the DCCL library

#include "goby/acomms/queue.h"
#include "goby/acomms/connect.h"
#include "simple.pb.h"
#include "goby/util/binary.h"
#include <iostream>


using goby::acomms::operator<<;

void received_data(const google::protobuf::Message& msg);

int main()
{
    //
    //  1. Initialize the QueueManager     
    //    
    goby::acomms::QueueManager q_manager;
    
    // our modem id (arbitrary, but must be unique in the network)
    const int our_id = 1;

    goby::acomms::protobuf::QueueManagerConfig cfg;
    cfg.set_modem_id(our_id);
    cfg.add_message_entry()->set_protobuf_name("Simple");
    q_manager.set_cfg(cfg);
    
    // set up the callback to handle received DCCL messages
    goby::acomms::connect(&q_manager.signal_receive, &received_data);
    
    // 
    //  2. Push a message to a queue 
    //
    
    // let's make a message to store in the queue
    Simple msg;
    msg.set_telegram("hello all!");
    q_manager.push_message(msg);
    
    std::cout << "1. pushing message to queue 1: " << msg << std::endl;

    // see what our QueueManager contains
    std::cout << "2. " << q_manager << std::endl;
    
    //
    //  3. Create a loopback to simulate the Link Layer (libmodemdriver & modem firmware) 
    //

    std::cout << "3. executing loopback (simulating sending a message to ourselves over the modem link)" << std::endl;
    
    // pretend the modem is requesting data of up to 32 bytes
    
    goby::acomms::protobuf::ModemTransmission request_msg;
    request_msg.set_max_frame_bytes(32);
    request_msg.set_max_num_frames(1);
    
    q_manager.handle_modem_data_request(&request_msg);
    
    std::cout << "4. requesting data, got: " << request_msg << std::endl;
    std::cout << "\tdata as hex: " << goby::util::hex_encode(request_msg.frame(0)) << std::endl;
    
    // 
    //  4. Pass the received message to the QueueManager (same as outgoing message) 
    //
    q_manager.handle_modem_receive(request_msg);
    
    return 0;
}

//
//  5. Do something with the received message  
//
void received_data(const google::protobuf::Message& msg)
{
    std::cout << "5. received message: " << msg << std::endl;
}

