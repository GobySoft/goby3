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

#include "goby/acomms/acomms_constants.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/dccl.h"
#include "goby/acomms/queue.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

#include "test.pb.h"
// tests basic DCCL queuing with non-BROADCAST destination

int receive_count = 0;
GobyMessage msg_in_macrura, msg_in_broadcast, msg_in_unicorn;
goby::acomms::protobuf::QueueManagerConfig cfg;
const int MY_MODEM_ID = 1;
const int UNICORN_MODEM_ID = 3;
const int MACRURA_MODEM_ID = 4;
goby::time::MicroTime
    current_time(boost::units::round(goby::time::SystemClock::now<goby::time::SITime>()));

int goby_message_qsize = 0;

int handle_ack_count = 0;

void handle_receive(const google::protobuf::Message& msg);

void qsize(goby::acomms::protobuf::QueueSize size);
void handle_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                const google::protobuf::Message& orig_msg);

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cout);
    goby::glog.set_name(argv[0]);

    goby::acomms::DCCLCodec* codec = goby::acomms::DCCLCodec::get();

    goby::acomms::protobuf::DCCLConfig dccl_cfg;
    dccl_cfg.set_crypto_passphrase("my_passphrase!");
    codec->set_cfg(dccl_cfg);

    goby::acomms::QueueManager q_manager;
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

    msg_in_macrura.set_telegram("hello mac!");
    msg_in_macrura.mutable_header()->set_time_with_units(current_time);
    msg_in_macrura.mutable_header()->set_source_platform(MY_MODEM_ID);
    msg_in_macrura.mutable_header()->set_dest_platform(MACRURA_MODEM_ID);
    msg_in_macrura.mutable_header()->set_dest_type(Header::PUBLISH_OTHER);

    msg_in_broadcast.set_telegram("hello all!");
    msg_in_broadcast.mutable_header()->set_time_with_units(current_time);

    msg_in_broadcast.mutable_header()->set_source_platform(MY_MODEM_ID);
    msg_in_broadcast.mutable_header()->set_dest_type(Header::PUBLISH_ALL);

    msg_in_unicorn.set_telegram("hello uni!");
    msg_in_unicorn.mutable_header()->set_time_with_units(current_time);
    msg_in_unicorn.mutable_header()->set_source_platform(MY_MODEM_ID);
    msg_in_unicorn.mutable_header()->set_dest_platform(UNICORN_MODEM_ID);
    msg_in_unicorn.mutable_header()->set_dest_type(Header::PUBLISH_OTHER);

    std::cout << "Pushed: " << msg_in_broadcast << std::endl;
    q_manager.push_message(msg_in_broadcast);
    assert(goby_message_qsize == 1);

    std::cout << "Pushed: " << msg_in_macrura << std::endl;
    q_manager.push_message(msg_in_macrura);
    assert(goby_message_qsize == 2);

    goby::acomms::protobuf::ModemTransmission transmit_msg;

    // first send to Macrura, who is AWOL
    transmit_msg.set_max_frame_bytes(256);
    transmit_msg.set_dest(goby::acomms::QUERY_DESTINATION_ID);

    q_manager.handle_modem_data_request(&transmit_msg);

    // message without ack is sent and popped
    assert(goby_message_qsize == 1);

    // repush the broadcast message
    std::cout << "Pushed: " << msg_in_broadcast << std::endl;
    q_manager.push_message(msg_in_broadcast);
    assert(goby_message_qsize == 2);

    // push one for unicorn
    std::cout << "Pushed: " << msg_in_unicorn << std::endl;
    q_manager.push_message(msg_in_unicorn);
    assert(goby_message_qsize == 3);

    transmit_msg.Clear();
    transmit_msg.set_max_frame_bytes(256);
    transmit_msg.set_dest(goby::acomms::QUERY_DESTINATION_ID);

    q_manager.handle_modem_data_request(&transmit_msg);

    // two ack, one not
    assert(goby_message_qsize == 2);

    std::cout << "requesting data, got: " << transmit_msg << std::endl;
    std::cout << "\tdata as hex: " << goby::util::hex_encode(transmit_msg.frame(0)) << std::endl;

    std::list<const google::protobuf::Message*> msgs;
    msgs.push_back(&msg_in_unicorn);
    msgs.push_back(&msg_in_broadcast);

    assert(transmit_msg.frame(0) == goby::acomms::DCCLCodec::get()->encode_repeated(msgs));
    assert(transmit_msg.src() == MY_MODEM_ID);
    assert(transmit_msg.dest() == UNICORN_MODEM_ID);
    assert(transmit_msg.ack_requested() == true);

    // feed back the modem layer - both messages will be rejected
    q_manager.handle_modem_receive(transmit_msg);
    assert(receive_count == 0);

    // fake an ack from unicorn
    goby::acomms::protobuf::ModemTransmission ack;
    ack.set_src(UNICORN_MODEM_ID);
    ack.set_dest(MY_MODEM_ID);
    ack.add_acked_frame(0);
    ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
    q_manager.handle_modem_receive(ack);

    assert(goby_message_qsize == 1);
    assert(handle_ack_count == 1);

    // pretend we're now Unicorn
    cfg.set_modem_id(UNICORN_MODEM_ID);
    q_manager.set_cfg(cfg);

    // feed back the modem layer
    q_manager.handle_modem_receive(transmit_msg);

    assert(receive_count == 2);

    std::cout << "all tests passed" << std::endl;

    dccl::DynamicProtobufManager::protobuf_shutdown();
}

void handle_receive(const google::protobuf::Message& msg)
{
    std::cout << "Received: " << msg << std::endl;

    GobyMessage typed_msg;
    typed_msg.CopyFrom(msg);

    assert(typed_msg.header().time_with_units() == current_time);

    if (!typed_msg.header().has_dest_platform())
        assert(typed_msg.SerializeAsString() == msg_in_broadcast.SerializeAsString());
    else if (typed_msg.header().dest_platform() == 3)
        assert(typed_msg.SerializeAsString() == msg_in_unicorn.SerializeAsString());

    ++receive_count;
}

void qsize(goby::acomms::protobuf::QueueSize size) { goby_message_qsize = size.size(); }

void handle_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                const google::protobuf::Message& orig_msg)
{
    std::cout << "got an ack: " << ack_msg << "\n"
              << "of original: " << orig_msg << std::endl;
    assert(orig_msg.SerializeAsString() == msg_in_unicorn.SerializeAsString());
    ++handle_ack_count;
}
