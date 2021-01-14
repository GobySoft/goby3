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
#include "goby/acomms/protobuf/modem_message.pb.h"
#include "goby/acomms/queue.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

#include "goby/test/acomms/queue1/test.pb.h"

using goby::test::acomms::protobuf::TestMsg;

// tests basic DCCL queuing

int receive_count = 0;
TestMsg test_msg1;

void handle_receive(const google::protobuf::Message& msg);

int main(int /*argc*/, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    goby::acomms::QueueManager q_manager;
    goby::acomms::protobuf::QueueManagerConfig cfg;
    const int MY_MODEM_ID = 1;
    cfg.set_modem_id(MY_MODEM_ID);
    cfg.add_message_entry()->set_protobuf_name("goby.test.acomms.protobuf.TestMsg");

    q_manager.set_cfg(cfg);

    goby::acomms::connect(&q_manager.signal_receive, &handle_receive);

    test_msg1.set_double_default_optional(1.23);
    test_msg1.set_float_default_optional(0.2);

    std::cout << "Pushed: " << test_msg1 << std::endl;
    q_manager.push_message(test_msg1);

    goby::acomms::protobuf::ModemTransmission msg;
    msg.set_max_frame_bytes(256);
    q_manager.handle_modem_data_request(&msg);

    std::cout << "requesting data, got: " << msg << std::endl;
    std::cout << "\tdata as hex: " << goby::util::hex_encode(msg.frame(0)) << std::endl;

    std::string encoded;
    goby::acomms::DCCLCodec::get()->encode(&encoded, test_msg1);

    assert(msg.frame(0) == encoded);
    assert(msg.src() == MY_MODEM_ID);
    assert(msg.dest() == goby::acomms::BROADCAST_ID);

    assert(msg.ack_requested() == false);

    // feed back the modem layer
    q_manager.handle_modem_receive(msg);

    assert(receive_count == 1);

    std::cout << "all tests passed" << std::endl;

    dccl::DynamicProtobufManager::protobuf_shutdown();
}

void handle_receive(const google::protobuf::Message& msg)
{
    std::cout << "Received: " << msg << std::endl;

    assert(test_msg1.SerializeAsString() == msg.SerializeAsString());

    ++receive_count;
}
