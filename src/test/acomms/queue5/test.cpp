// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Ryan Govostes <rgovostes+git@gmail.com>
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
#include "goby/acomms/queue.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

#include "test.pb.h"

// tests "encode_on_demand" functionality
using goby::test::acomms::protobuf::GobyMessage;

int receive_count = 0;
int encode_on_demand_count = 0;
bool handle_ack_called = false;
int goby_message_qsize = 0;
int last_decoded_count = 0;
std::deque<int> decode_order;
//GobyMessage msg_in1, msg_in2;
goby::acomms::QueueManager q_manager;
const int MY_MODEM_ID = 1;
bool provide_data = true;

void handle_encode_on_demand(const goby::acomms::protobuf::ModemTransmission& request_msg,
                             google::protobuf::Message* data_msg);

void qsize(goby::acomms::protobuf::QueueSize size);

void handle_receive(const google::protobuf::Message& msg);

void request_test(int request_bytes, int expected_encode_requests, int expected_messages_sent);

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    goby::acomms::DCCLCodec* codec = goby::acomms::DCCLCodec::get();

    goby::acomms::protobuf::QueueManagerConfig cfg;
    cfg.set_modem_id(MY_MODEM_ID);
    goby::acomms::protobuf::QueuedMessageEntry* entry = cfg.add_message_entry();
    entry->set_protobuf_name("goby.test.acomms.protobuf.GobyMessage");
    entry->set_newest_first(true);
    entry->add_manipulator(goby::acomms::protobuf::ON_DEMAND);
    cfg.set_on_demand_skew_seconds(0.1);

    q_manager.set_cfg(cfg);

    goby::glog << q_manager << std::endl;

    goby::acomms::connect(&q_manager.signal_queue_size_change, &qsize);
    goby::acomms::connect(&q_manager.signal_data_on_demand, &handle_encode_on_demand);
    goby::acomms::connect(&q_manager.signal_receive, &handle_receive);

    // we want to test requesting for a message slightly larger than the expected size
    decode_order.push_back(0);
    request_test(codec->size(GobyMessage()) + 1, 2, 1);
    assert(decode_order.empty());

    // no lag so it should be the *already* encoded message
    decode_order.push_back(1);
    request_test(codec->size(GobyMessage()), 0, 1);
    assert(decode_order.empty());

    // nothing in the queue so we'll try this again
    decode_order.push_back(2);
    decode_order.push_back(3);
    request_test(codec->size(GobyMessage()) * 2 + 1, 3, 2);
    assert(decode_order.empty());

    // 0.2 seconds > 0.1 seconds
    usleep(2e5);
    // 4 is too old so a new request made
    decode_order.push_back(5);
    request_test(codec->size(GobyMessage()), 1, 1);
    assert(decode_order.empty());

    // we won't provide data this time so the old message in the queue (4) should be sent
    provide_data = false;
    decode_order.push_back(4);
    request_test(codec->size(GobyMessage()), 1, 1);
    assert(decode_order.empty());

    std::cout << "all tests passed" << std::endl;

    dccl::DynamicProtobufManager::protobuf_shutdown();
}

void request_test(int request_bytes, int expected_encode_requests, int expected_messages_sent)
{
    int starting_qsize = goby_message_qsize;
    int starting_encode_count = encode_on_demand_count;

    goby::acomms::protobuf::ModemTransmission transmit_msg;
    transmit_msg.set_max_frame_bytes(request_bytes);
    transmit_msg.set_max_num_frames(1);

    q_manager.handle_modem_data_request(&transmit_msg);
    std::cout << "requesting data, got: " << transmit_msg << std::endl;

    // once for each one that fits, twice for the one that doesn't
    assert(encode_on_demand_count - starting_encode_count == expected_encode_requests);

    std::cout << "\tdata as hex: " << goby::util::hex_encode(transmit_msg.frame(0)) << std::endl;

    assert(transmit_msg.src() == MY_MODEM_ID);
    assert(transmit_msg.dest() == goby::acomms::BROADCAST_ID);
    assert(transmit_msg.ack_requested() == false);

    if (provide_data)
        assert(goby_message_qsize ==
               starting_qsize + expected_encode_requests - expected_messages_sent);
    else
        assert(goby_message_qsize == starting_qsize - expected_messages_sent);

    q_manager.handle_modem_receive(transmit_msg);
}

void handle_encode_on_demand(const goby::acomms::protobuf::ModemTransmission& request_msg,
                             google::protobuf::Message* data_msg)
{
    GobyMessage msg;

    if (provide_data)
        msg.set_telegram(encode_on_demand_count);

    std::cout << "encoded on demand: " << msg << std::endl;

    // put our message into the data_msg for return
    data_msg->CopyFrom(msg);
    ++encode_on_demand_count;
}

void qsize(goby::acomms::protobuf::QueueSize size) { goby_message_qsize = size.size(); }

void handle_receive(const google::protobuf::Message& in_msg)
{
    GobyMessage msg;
    msg.CopyFrom(in_msg);

    std::cout << "Received: " << msg << std::endl;
    ++receive_count;
    assert(decode_order.front() == msg.telegram());
    decode_order.pop_front();
}
