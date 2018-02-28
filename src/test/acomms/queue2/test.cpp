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

#include "test.pb.h"
#include "goby/acomms/queue.h"
#include "goby/acomms/connect.h"
#include "goby/util/binary.h"
#include "goby/common/logger.h"
#include "goby/acomms/dccl.h"
// tests basic DCCL queuing with non-BROADCAST destination

using goby::acomms::operator<<;

int receive_count = 0;
GobyMessage msg_in1;

void handle_receive(const google::protobuf::Message &msg);

int main(int argc, char* argv[])
{    
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    
    goby::acomms::QueueManager q_manager;
    goby::acomms::protobuf::QueueManagerConfig cfg;
    const int MY_MODEM_ID = 1;
    const int UNICORN_MODEM_ID = 3;
    cfg.set_modem_id(MY_MODEM_ID);
    goby::acomms::protobuf::QueuedMessageEntry* q_entry = cfg.add_message_entry();
    q_entry->set_protobuf_name("GobyMessage");
    q_entry->set_newest_first(true);
    
    goby::acomms::protobuf::QueuedMessageEntry::Role* dest_role = q_entry->add_role();
    dest_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::DESTINATION_ID);
    dest_role->set_field("header.dest_platform");    

    goby::acomms::protobuf::QueuedMessageEntry::Role* time_role = q_entry->add_role();
    time_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::TIMESTAMP);
    time_role->set_field("header.time");    

    goby::acomms::protobuf::QueuedMessageEntry::Role* src_role = q_entry->add_role();
    src_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::SOURCE_ID);
    // intentionally misspelled
    src_role->set_field("hder.source_platform");
    
    try
    {    
        q_manager.set_cfg(cfg);
        bool FAILED_CHECK_FIELDS = false;
        assert(FAILED_CHECK_FIELDS);
    }
    catch(goby::acomms::QueueException&)
    {
        // good
    }

    // fix the misspelling and try again
    src_role->set_field("header.source_platform");
    q_manager.set_cfg(cfg);
    
    goby::acomms::connect(&q_manager.signal_receive, &handle_receive);

    msg_in1.set_telegram("hello!");
    msg_in1.mutable_header()->set_time(
        goby::util::as<goby::uint64>(boost::posix_time::second_clock::universal_time()));
    msg_in1.mutable_header()->set_source_platform(MY_MODEM_ID);
    msg_in1.mutable_header()->set_dest_platform(UNICORN_MODEM_ID);
    msg_in1.mutable_header()->set_dest_type(Header::PUBLISH_OTHER);
    

    std::cout << "Pushed: " << msg_in1 << std::endl;
    q_manager.push_message(msg_in1);


    goby::acomms::protobuf::ModemTransmission transmit_msg;
    transmit_msg.set_max_frame_bytes(256);
    transmit_msg.set_dest(UNICORN_MODEM_ID);
    
    q_manager.handle_modem_data_request(&transmit_msg);
    

    std::cout << "requesting data, got: " << transmit_msg << std::endl;
    std::cout << "\tdata as hex: " << goby::util::hex_encode(transmit_msg.frame(0)) << std::endl;

    std::string encoded;
    goby::acomms::DCCLCodec::get()->encode(&encoded, msg_in1);
    
    assert(transmit_msg.frame(0) == encoded);
    assert(transmit_msg.src() == MY_MODEM_ID);
    assert(transmit_msg.dest() == UNICORN_MODEM_ID);
    assert(transmit_msg.ack_requested() == true);
    

    // feed back the modem layer - this will be rejected
    q_manager.handle_modem_receive(transmit_msg);
    assert(receive_count == 0);

    
    // pretend we're now Unicorn
    cfg.set_modem_id(UNICORN_MODEM_ID);
    q_manager.set_cfg(cfg);
    
    // feed back the modem layer
    q_manager.handle_modem_receive(transmit_msg);
    
    assert(receive_count == 1);
    
    std::cout << "all tests passed" << std::endl;    
}

void handle_receive(const google::protobuf::Message &msg)
{
    std::cout << "Received: " << msg << std::endl;
    
    assert(msg_in1.SerializeAsString() == msg.SerializeAsString());
    
    ++receive_count;
}
