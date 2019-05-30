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

// tests 0.1 --> route_manager --> 1.1 --> 2.1 route

// id     1.1                 2.1                3.1
//
//         ------->Router-------                 out
//         |                   |                  ^
//         |                   v                  |
//    QueueManager         QueueManager       QueueManager
//         ^                   |                  ^
//         |                   v                  |
//        in                 Driver  ------>    Driver

#include "goby/acomms/amac/mac_manager.h"
#include "goby/acomms/bind.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include "goby/acomms/route/route.h"
#include "goby/time.h"
#include "goby/util/as.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "test.pb.h"
#include <cstdlib>

using namespace goby::common::logger;
using namespace goby::acomms;
using goby::common::goby_time;
using goby::util::as;
using namespace boost::posix_time;

const int ID_0_1 = 1;
const int ID_1_1 = (1 << 8) + 1;
const int ID_2_1 = (2 << 8) + 1;
const int ID_3_1 = (3 << 8) + 1;

void handle_receive1(const google::protobuf::Message& msg);
void handle_receive2(const google::protobuf::Message& msg);
void handle_receive3(const google::protobuf::Message& msg);

void handle_modem_receive3(const protobuf::ModemTransmission& message);

boost::asio::io_service io2, io3;
std::shared_ptr<goby::acomms::UDPDriver> driver2, driver3;

bool received_message = false;

RouteMessage in_msg;

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::clog);
    goby::glog.set_name(argv[0]);

    // set up queues
    goby::acomms::QueueManager q_manager1, q_manager2, q_manager3;
    goby::acomms::protobuf::QueueManagerConfig q_cfg1, q_cfg2, q_cfg3;
    goby::acomms::protobuf::QueuedMessageEntry* q_entry = q_cfg1.add_message_entry();
    q_entry->set_protobuf_name("RouteMessage");
    q_entry->set_newest_first(true);

    goby::acomms::protobuf::QueuedMessageEntry::Role* src_role = q_entry->add_role();
    src_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::SOURCE_ID);
    src_role->set_field("src");

    goby::acomms::protobuf::QueuedMessageEntry::Role* dest_role = q_entry->add_role();
    dest_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::DESTINATION_ID);
    dest_role->set_field("dest");

    goby::acomms::protobuf::QueuedMessageEntry::Role* time_role = q_entry->add_role();
    time_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::TIMESTAMP);
    time_role->set_field("time");

    q_cfg2.add_message_entry()->CopyFrom(*q_entry);
    q_cfg3.add_message_entry()->CopyFrom(*q_entry);

    q_cfg1.set_skip_decoding(true);
    q_cfg2.set_skip_decoding(true);
    q_cfg3.set_skip_decoding(false);

    q_cfg1.set_modem_id(ID_1_1);
    q_manager1.set_cfg(q_cfg1);

    q_cfg2.set_modem_id(ID_2_1);
    q_manager2.set_cfg(q_cfg2);

    q_cfg3.set_modem_id(ID_3_1);
    q_manager3.set_cfg(q_cfg3);

    // connect our receive callback
    goby::acomms::connect(&q_manager1.signal_receive, &handle_receive1);
    goby::acomms::connect(&q_manager2.signal_receive, &handle_receive2);
    goby::acomms::connect(&q_manager3.signal_receive, &handle_receive3);

    // set up the router
    goby::acomms::RouteManager r_manager;
    goby::acomms::protobuf::RouteManagerConfig r_cfg;
    r_cfg.mutable_route()->add_hop(ID_0_1);
    r_cfg.mutable_route()->add_hop(ID_1_1);
    r_cfg.mutable_route()->add_hop(ID_2_1);
    r_cfg.mutable_route()->add_hop(ID_3_1);
    r_cfg.set_subnet_mask(0xFFFFFF00);
    r_manager.set_cfg(r_cfg);

    // set up drivers
    driver2.reset(new goby::acomms::UDPDriver(&io2));
    driver3.reset(new goby::acomms::UDPDriver(&io3));

    goby::acomms::protobuf::DriverConfig d_cfg2, d_cfg3;
    d_cfg2.set_modem_id(ID_2_1);
    d_cfg3.set_modem_id(ID_3_1);

    srand(time(NULL));
    int port1 = rand() % 1000 + 50020;
    int port2 = port1 + 1;

    d_cfg2.MutableExtension(UDPDriverConfig::local)->set_port(port1);
    d_cfg3.MutableExtension(UDPDriverConfig::local)->set_port(port2);
    d_cfg2.MutableExtension(UDPDriverConfig::remote)
        ->CopyFrom(d_cfg3.GetExtension(UDPDriverConfig::local));
    d_cfg3.MutableExtension(UDPDriverConfig::remote)
        ->CopyFrom(d_cfg2.GetExtension(UDPDriverConfig::local));

    driver2->startup(d_cfg2);
    driver3->startup(d_cfg3);

    // set up two MACManagers to handle the drivers
    goby::acomms::MACManager mac2, mac3;
    goby::acomms::protobuf::MACConfig m_cfg2, m_cfg3;
    m_cfg2.set_modem_id(ID_2_1);
    m_cfg3.set_modem_id(ID_3_1);
    m_cfg2.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);
    m_cfg3.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);

    goby::acomms::protobuf::ModemTransmission slot2;
    slot2.set_src(ID_2_1);
    slot2.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    slot2.set_slot_seconds(0.1);
    m_cfg2.add_slot()->CopyFrom(slot2);
    m_cfg3.add_slot()->CopyFrom(slot2);

    goby::acomms::protobuf::ModemTransmission slot3;
    slot3.set_src(ID_3_1);
    slot3.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    slot3.set_slot_seconds(0.1);
    m_cfg2.add_slot()->CopyFrom(slot3);
    m_cfg3.add_slot()->CopyFrom(slot3);

    mac2.startup(m_cfg2);
    mac3.startup(m_cfg3);

    // renable crypto when we receive on driver3
    goby::acomms::connect(&driver3->signal_receive, &handle_modem_receive3);

    // bind them all together
    goby::acomms::bind(*driver2, q_manager2, mac2);
    goby::acomms::bind(*driver3, q_manager3, mac3);

    goby::acomms::bind(q_manager1, r_manager);
    goby::acomms::bind(q_manager2, r_manager);

    // create a message
    in_msg.set_src(ID_0_1);
    in_msg.set_dest(ID_3_1);
    in_msg.set_time_with_units(boost::units::round(
        goby::time::SystemClock::now<goby::time::SITime>())); // integer second for _time codec
    in_msg.set_telegram("0-->3");

    // create transmission from message
    goby::acomms::protobuf::ModemTransmission initial_transmit;
    initial_transmit.set_src(ID_0_1);
    initial_transmit.set_dest(ID_1_1);
    initial_transmit.set_time_with_units(goby::time::SystemClock::now<goby::time::MicroTime>());
    initial_transmit.set_type(goby::acomms::protobuf::ModemTransmission::DATA);

    goby::acomms::DCCLCodec* dccl = goby::acomms::DCCLCodec::get();

    goby::acomms::protobuf::DCCLConfig dccl_cfg;
    dccl_cfg.set_crypto_passphrase("my_passphrase2!");
    dccl->set_cfg(dccl_cfg);

    dccl->validate<RouteMessage>();
    dccl->encode(initial_transmit.add_frame(), in_msg);

    // remove crypto
    dccl_cfg.clear_crypto_passphrase();
    dccl->set_cfg(dccl_cfg);

    // unleash the message
    std::cout << "Modem receive on q1: " << initial_transmit.DebugString() << std::endl;
    q_manager1.handle_modem_receive(initial_transmit);

    int i = 0;

    while (((i / 10) < 10))
    {
        driver2->do_work();
        driver3->do_work();

        q_manager1.do_work();
        q_manager2.do_work();
        q_manager3.do_work();

        mac2.do_work();
        mac3.do_work();

        usleep(100000);

        if (received_message)
            break;

        ++i;
    }

    if (received_message)
    {
        std::cout << "all tests passed" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "never received message" << std::endl;
        return 1;
    }
}

void handle_receive1(const google::protobuf::Message& msg) { assert(false); }

void handle_receive2(const google::protobuf::Message& msg) { assert(false); }

void handle_receive3(const google::protobuf::Message& msg)
{
    std::cout << "Received at 3.1: " << msg.DebugString() << std::endl;
    std::cout << "Original: " << in_msg.DebugString() << std::endl;
    received_message = true;
    assert(in_msg.SerializeAsString() == msg.SerializeAsString());
}

void handle_modem_receive3(const protobuf::ModemTransmission& message)
{
    goby::acomms::DCCLCodec* dccl = goby::acomms::DCCLCodec::get();
    goby::acomms::protobuf::DCCLConfig dccl_cfg;
    dccl_cfg.set_crypto_passphrase("my_passphrase2!");
    dccl->set_cfg(dccl_cfg);
}
