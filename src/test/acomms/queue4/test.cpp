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

#include "goby/acomms/connect.h"
#include "goby/acomms/dccl.h"
#include "goby/acomms/queue.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "test.pb.h"

// tests multi-frame DCCL queuing with non-BROADCAST destination

using goby::acomms::operator<<;

int receive_count = 0;
bool handle_ack_called = false;
int goby_message_qsize = 0;
GobyMessage msg_in1, msg_in2;

void handle_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                const google::protobuf::Message& orig_msg);

void qsize(goby::acomms::protobuf::QueueSize size);

void handle_receive(const google::protobuf::Message& msg);

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

    goby::acomms::protobuf::QueuedMessageEntry::Role* src_role = q_entry->add_role();
    src_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::SOURCE_ID);
    src_role->set_field("header.source_platform");

    goby::acomms::protobuf::QueuedMessageEntry::Role* dest_role = q_entry->add_role();
    dest_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::DESTINATION_ID);
    dest_role->set_field("header.dest_platform");

    goby::acomms::protobuf::QueuedMessageEntry::Role* time_role = q_entry->add_role();
    time_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::TIMESTAMP);
    time_role->set_field("header.time");

    q_manager.set_cfg(cfg);

    goby::acomms::connect(&q_manager.signal_receive, &handle_receive);
    goby::acomms::connect(&q_manager.signal_queue_size_change, &qsize);
    goby::acomms::connect(&q_manager.signal_ack, &handle_ack);

    msg_in1.set_telegram("hello!");
    msg_in1.mutable_header()->set_time_with_units(
        goby::time::SystemClock::now<goby::time::MicroTime>());
    msg_in1.mutable_header()->set_source_platform(MY_MODEM_ID);
    msg_in1.mutable_header()->set_dest_platform(UNICORN_MODEM_ID);
    msg_in1.mutable_header()->set_dest_type(Header::PUBLISH_OTHER);
    msg_in1.set_telegram("hello 1");

    msg_in2 = msg_in1;
    msg_in2.set_telegram("hello 2");

    std::cout << "Pushed: " << msg_in2 << std::endl;
    q_manager.push_message(msg_in2);
    std::cout << "Pushed: " << msg_in1 << std::endl;
    q_manager.push_message(msg_in1);

    goby::acomms::protobuf::ModemTransmission transmit_msg;
    transmit_msg.set_max_frame_bytes(16);
    transmit_msg.set_max_num_frames(2);
    transmit_msg.set_dest(UNICORN_MODEM_ID);
    q_manager.handle_modem_data_request(&transmit_msg);

    std::cout << "requesting data, got: " << transmit_msg << std::endl;
    std::cout << "\tdata frame 0 as hex: " << goby::util::hex_encode(transmit_msg.frame(0))
              << std::endl;
    std::cout << "\tdata frame 1 as hex: " << goby::util::hex_encode(transmit_msg.frame(1))
              << std::endl;

    // fake an ack from unicorn
    goby::acomms::protobuf::ModemTransmission ack;
    ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
    ack.set_src(UNICORN_MODEM_ID);
    ack.set_dest(MY_MODEM_ID);
    ack.add_acked_frame(0);
    ack.add_acked_frame(1);
    q_manager.handle_modem_receive(ack);

    assert(goby_message_qsize == 0);
    assert(handle_ack_called == true);

    std::cout << "all tests passed" << std::endl;

    dccl::DynamicProtobufManager::protobuf_shutdown();
}

void handle_receive(const google::protobuf::Message& msg)
{
    std::cout << "Received: " << msg << std::endl;

    ++receive_count;
}

void qsize(goby::acomms::protobuf::QueueSize size) { goby_message_qsize = size.size(); }

void handle_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                const google::protobuf::Message& orig_msg)
{
    std::cout << "got an ack: " << ack_msg << "\n"
              << "of original: " << orig_msg << std::endl;
    handle_ack_called = true;
}
