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
#include "goby/acomms/queue.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "test.pb.h"

// tests various manipulators' functionality

using goby::acomms::operator<<;

goby::acomms::QueueManager q_manager;
const int MY_MODEM_ID = 1;
const unsigned TEST_MESSAGE_SIZE = 3;

int receive_count = 0;

void qsize(goby::acomms::protobuf::QueueSize size);

void handle_receive(const google::protobuf::Message& msg);

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    goby::acomms::protobuf::QueueManagerConfig cfg;
    cfg.set_modem_id(MY_MODEM_ID);
    goby::acomms::protobuf::QueuedMessageEntry* q_entry = cfg.add_message_entry();
    q_entry->set_protobuf_name("GobyMessage");
    goby::acomms::protobuf::QueuedMessageEntry::Role* dest_role = q_entry->add_role();
    dest_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::DESTINATION_ID);
    dest_role->set_field("dest");

    q_entry->set_ack(false);
    q_manager.set_cfg(cfg);

    goby::glog << q_manager << std::endl;

    goby::acomms::connect(&q_manager.signal_queue_size_change, &qsize);
    goby::acomms::connect(&q_manager.signal_receive, &handle_receive);

    GobyMessage test_msg1;
    test_msg1.set_dest(0);
    test_msg1.set_telegram(1);
    std::cout << "Pushed: " << test_msg1 << std::endl;
    q_manager.push_message(test_msg1);

    goby::acomms::protobuf::ModemTransmission msg;
    msg.set_max_frame_bytes(32);
    q_manager.handle_modem_data_request(&msg);

    // message was queued
    assert(msg.frame(0).size() == TEST_MESSAGE_SIZE);
    // feed back the modem layer
    q_manager.handle_modem_receive(msg);
    // message was decoded
    assert(receive_count == 1);

    {
        receive_count = 0;
        cfg.mutable_message_entry(0)->clear_manipulator();
        cfg.mutable_message_entry(0)->add_manipulator(goby::acomms::protobuf::NO_DECODE);
        q_manager.set_cfg(cfg);

        q_manager.push_message(test_msg1);
        msg.Clear();
        msg.set_max_frame_bytes(32);
        q_manager.handle_modem_data_request(&msg);
        // message was queued
        assert(msg.frame(0).size() == TEST_MESSAGE_SIZE);
        q_manager.handle_modem_receive(msg);
        // message was not decoded
        assert(receive_count == 0);
    }

    {
        receive_count = 0;
        cfg.mutable_message_entry(0)->clear_manipulator();
        cfg.mutable_message_entry(0)->add_manipulator(goby::acomms::protobuf::NO_QUEUE);
        q_manager.set_cfg(cfg);

        q_manager.push_message(test_msg1);
        msg.Clear();
        msg.set_max_frame_bytes(32);
        q_manager.handle_modem_data_request(&msg);
        // message was not queued
        assert(msg.frame(0).size() == 0);
        q_manager.handle_modem_receive(msg);
        // message was not decoded
        assert(receive_count == 0);
    }

    {
        receive_count = 0;
        cfg.mutable_message_entry(0)->clear_manipulator();
        cfg.mutable_message_entry(0)->add_manipulator(goby::acomms::protobuf::LOOPBACK);
        q_manager.set_cfg(cfg);

        q_manager.push_message(test_msg1);
        // loop'd back
        assert(receive_count == 1);

        msg.Clear();
        msg.set_max_frame_bytes(32);
        q_manager.handle_modem_data_request(&msg);
        // message was queued
        assert(msg.frame(0).size() == TEST_MESSAGE_SIZE);
        q_manager.handle_modem_receive(msg);
        // message was decoded
        assert(receive_count == 2);
    }

    {
        receive_count = 0;
        cfg.mutable_message_entry(0)->clear_manipulator();
        cfg.mutable_message_entry(0)->add_manipulator(goby::acomms::protobuf::LOOPBACK_AS_SENT);
        q_manager.set_cfg(cfg);

        q_manager.push_message(test_msg1);
        // not loop'd back (yet)
        assert(receive_count == 0);

        msg.Clear();
        msg.set_max_frame_bytes(32);
        q_manager.handle_modem_data_request(&msg);

        // loop'd back
        assert(receive_count == 1);

        // message was queued
        assert(msg.frame(0).size() == TEST_MESSAGE_SIZE);
        q_manager.handle_modem_receive(msg);
        // message was decoded
        assert(receive_count == 2);
    }

    {
        test_msg1.set_dest(3);

        receive_count = 0;
        cfg.mutable_message_entry(0)->clear_manipulator();
        q_manager.set_cfg(cfg);

        q_manager.push_message(test_msg1);

        msg.Clear();
        msg.set_max_frame_bytes(32);
        q_manager.handle_modem_data_request(&msg);

        // message was queued
        assert(msg.frame(0).size() == TEST_MESSAGE_SIZE);
        q_manager.handle_modem_receive(msg);
        // message was decoded (due to wrong destination)
        assert(receive_count == 0);

        receive_count = 0;
        cfg.mutable_message_entry(0)->clear_manipulator();
        cfg.mutable_message_entry(0)->add_manipulator(goby::acomms::protobuf::PROMISCUOUS);
        q_manager.set_cfg(cfg);

        q_manager.push_message(test_msg1);

        msg.Clear();
        msg.set_max_frame_bytes(32);
        q_manager.handle_modem_data_request(&msg);

        // message was queued
        assert(msg.frame(0).size() == TEST_MESSAGE_SIZE);
        q_manager.handle_modem_receive(msg);
        // message was decoded (due to promiscuous)
        assert(receive_count == 1);
    }

    std::cout << "all tests passed" << std::endl;

    dccl::DynamicProtobufManager::protobuf_shutdown();
}

void qsize(goby::acomms::protobuf::QueueSize size) {}

void handle_receive(const google::protobuf::Message& in_msg)
{
    std::cout << "Received: " << in_msg << std::endl;
    ++receive_count;
}
