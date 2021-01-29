// Copyright 2015-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

// tests functionality of the UDPDriver with respect to multiple frames & acknowledgments

#include <cstdlib>

#include "goby/acomms/bind.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include "goby/acomms/protobuf/udp_driver.pb.h"
#include "goby/acomms/queue.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

#include "goby/test/acomms/dccl3/test.pb.h"

using goby::acomms::udp::protobuf::config;
using goby::test::acomms::protobuf::GobyMessage;
using goby::test::acomms::protobuf::Header;

std::shared_ptr<goby::acomms::ModemDriverBase> driver1, driver2;
goby::acomms::QueueManager q1, q2;
GobyMessage msg_in1, msg_in2;
bool received_ack = false;

using namespace goby::acomms;

void handle_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                const google::protobuf::Message& orig_msg);

void qsize(const goby::acomms::protobuf::QueueSize& size);

void handle_receive(const google::protobuf::Message& msg);
void handle_data_receive1(const protobuf::ModemTransmission& msg);

void test5()
{
    msg_in1.set_telegram("hello!");
    msg_in1.mutable_header()->set_time_with_units(
        goby::time::SystemClock::now<goby::time::MicroTime>());
    msg_in1.mutable_header()->set_source_platform(1);
    msg_in1.mutable_header()->set_dest_platform(2);
    msg_in1.mutable_header()->set_dest_type(Header::PUBLISH_OTHER);
    msg_in1.set_telegram("hello 1");

    msg_in2 = msg_in1;
    msg_in2.set_telegram("hello 2");

    std::cout << "Pushed: " << msg_in2 << std::endl;
    q1.push_message(msg_in2);
    std::cout << "Pushed: " << msg_in1 << std::endl;
    q1.push_message(msg_in1);

    protobuf::ModemTransmission transmit;

    transmit.set_type(protobuf::ModemTransmission::DATA);
    transmit.set_src(1);
    transmit.set_dest(2);
    transmit.set_rate(2);
    transmit.set_ack_requested(true);
    transmit.set_max_frame_bytes(15);

    // spoof a data request
    transmit.set_frame_start(0);
    q1.handle_modem_data_request(&transmit);
    assert(transmit.frame_size() == 1);
    // clear the data "lose it in transmission"
    transmit.clear_frame();

    transmit.set_frame_start(1);
    driver1->handle_initiate_transmission(transmit);

    int i = 0;
    while (((i / 10) < 60) && !received_ack)
    {
        driver1->do_work();
        driver2->do_work();

        usleep(100000);
        ++i;
    }

    usleep(100000);
    transmit.clear_frame();
    transmit.set_frame_start(2);
    q1.handle_modem_data_request(&transmit);
    assert(transmit.frame_size() == 1);
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::util::logger::DEBUG3, &fout);
    }

    goby::glog.set_name(argv[0]);

    driver1.reset(new goby::acomms::UDPDriver);
    driver2.reset(new goby::acomms::UDPDriver);

    goby::acomms::protobuf::DriverConfig cfg1, cfg2;
    auto* udp_cfg1 = cfg1.MutableExtension(config);
    auto* udp_cfg2 = cfg2.MutableExtension(config);

    cfg1.set_modem_id(1);

    srand(time(nullptr));
    int port1 = rand() % 1000 + 50010;
    int port2 = port1 + 1;

    //gumstix
    auto* local_endpoint1 = udp_cfg1->mutable_local();
    local_endpoint1->set_port(port1);

    cfg2.set_modem_id(2);

    // shore
    auto* local_endpoint2 = udp_cfg2->mutable_local();
    local_endpoint2->set_port(port2);

    auto* remote_endpoint1 = udp_cfg1->add_remote();

    remote_endpoint1->set_ip("localhost");
    remote_endpoint1->set_port(port2);

    auto* remote_endpoint2 = udp_cfg2->add_remote();

    remote_endpoint2->set_ip("127.0.0.1");
    remote_endpoint2->set_port(port1);

    udp_cfg1->set_max_frame_size(15);
    udp_cfg2->set_max_frame_size(15);

    goby::acomms::protobuf::QueueManagerConfig qcfg1, qcfg2;
    qcfg1.set_modem_id(1);
    qcfg1.set_minimum_ack_wait_seconds(.1);

    goby::acomms::protobuf::QueuedMessageEntry* q_entry = qcfg1.add_message_entry();
    q_entry->set_protobuf_name("goby.test.acomms.protobuf.GobyMessage");
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

    qcfg2 = qcfg1;
    qcfg2.set_modem_id(2);

    q1.set_cfg(qcfg1);
    q2.set_cfg(qcfg2);

    goby::glog.add_group("test", goby::util::Colors::green);
    goby::glog.add_group("driver1", goby::util::Colors::green);
    goby::glog.add_group("driver2", goby::util::Colors::yellow);

    goby::acomms::connect(&q2.signal_receive, &handle_receive);
    goby::acomms::connect(&q1.signal_queue_size_change, &qsize);
    goby::acomms::connect(&q1.signal_ack, &handle_ack);
    goby::acomms::bind(*driver1, q1);
    goby::acomms::bind(*driver2, q2);

    goby::acomms::connect(&driver1->signal_receive, handle_data_receive1);

    goby::glog << cfg1.DebugString() << std::endl;
    goby::glog << cfg2.DebugString() << std::endl;

    goby::glog << qcfg1.DebugString() << std::endl;
    goby::glog << qcfg2.DebugString() << std::endl;

    driver1->startup(cfg1);
    driver2->startup(cfg2);

    test5();

    std::cout << "all tests passed." << std::endl;
    return 0;
}

void handle_receive(const google::protobuf::Message& msg)
{
    std::cout << "Received: " << msg << std::endl;
}

void qsize(const goby::acomms::protobuf::QueueSize& size)
{
    std::cout << "Queue size: " << size.size() << std::endl;
}

void handle_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                const google::protobuf::Message& orig_msg)
{
    std::cout << "got an ack: " << ack_msg << "\n"
              << "of original: " << orig_msg << std::endl;
    assert(orig_msg.SerializeAsString() == msg_in2.SerializeAsString());
}

void handle_data_receive1(const protobuf::ModemTransmission& msg)
{
    if (msg.type() == protobuf::ModemTransmission::ACK)
        received_ack = true;
}
