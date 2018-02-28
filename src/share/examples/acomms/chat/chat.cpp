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

// usage: connect two modems and then run
// > chat /dev/tty_modem_A 1 2 log_file_A
// > chat /dev/tty_modem_B 2 1 log_file_B

// type into a window and hit enter to send a message. messages will be queued
// and sent on a fixed rotating cycle

#include <iostream>

#include "goby/acomms/dccl.h"
#include "goby/acomms/queue.h"
#include "goby/acomms/modem_driver.h"
#include "goby/acomms/amac.h"
#include "goby/acomms/bind.h"
#include "goby/util/as.h"
#include "goby/common/time.h"
#include "chat.pb.h"

#include <boost/lexical_cast.hpp>

#include "chat_curses.h"


// Uncomment to use the Micro-Modem 2 Flexible Data Packet instead of the traditional $CCCYC data cycle
//#define USE_FLEXIBLE_DATA_PACKET
// Uncomment to use the Micro-Modem ping $CCMPC instead of sending data
//#define USE_TWO_WAY_PING

using goby::util::as;
using goby::common::goby_time;

int startup_failure();
void received_data(const google::protobuf::Message&);
void received_ack(const goby::acomms::protobuf::ModemTransmission&,
                  const google::protobuf::Message&);
void monitor_mac(const goby::acomms::protobuf::ModemTransmission& mac_msg);
void monitor_modem_receive(const goby::acomms::protobuf::ModemTransmission& rx_msg);


std::ofstream fout_;
goby::acomms::DCCLCodec* dccl_ = goby::acomms::DCCLCodec::get();
goby::acomms::QueueManager q_manager_;
goby::acomms::MMDriver mm_driver_;
goby::acomms::MACManager mac_;
ChatCurses curses_;
int my_id_;
int buddy_id_;


int main(int argc, char* argv[])
{
    //
    // Deal with command line parameters
    //
    
    if(argc != 5) return startup_failure();
    
    std::string serial_port = argv[1];

    try
    {
        my_id_ = boost::lexical_cast<int>(argv[2]);
        buddy_id_ = boost::lexical_cast<int>(argv[3]);
    }
    catch(boost::bad_lexical_cast&)
    {
        std::cerr << "bad value for my_id: " << argv[2] << " or buddy_id: " << argv[3] << ". these must be unsigned integers." << std::endl;
        return startup_failure();        
    }

    std::string log_file = argv[4];
    fout_.open(log_file.c_str());
    if(!fout_.is_open())
    {
        std::cerr << "bad value for log_file: " << log_file << std::endl;
        return startup_failure();
    }

    //
    //  Initialize logging
    //
    goby::glog.add_stream(goby::common::logger::DEBUG1, &fout_);
    goby::glog.set_name(argv[0]);

    
    // bind the signals of these libraries
    bind(mm_driver_, q_manager_, mac_);
    
    //
    // Initiate DCCL (libdccl)
    //
    goby::acomms::protobuf::DCCLConfig dccl_cfg;
    
    dccl_->validate<ChatMessage>();    
    
    //
    // Initiate queue manager (libqueue)
    //
    goby::acomms::protobuf::QueueManagerConfig q_manager_cfg;
    q_manager_cfg.set_modem_id(my_id_);
    goby::acomms::protobuf::QueuedMessageEntry* q_entry = q_manager_cfg.add_message_entry();
    q_entry->set_protobuf_name("ChatMessage");

#ifdef USE_FLEXIBLE_DATA_PACKET
    q_entry->set_ack(false);
#endif
    
    goby::acomms::protobuf::QueuedMessageEntry::Role* src_role = q_entry->add_role();
    src_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::SOURCE_ID);
    src_role->set_field("source");
    
    goby::acomms::protobuf::QueuedMessageEntry::Role* dest_role = q_entry->add_role();
    dest_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::DESTINATION_ID);
    dest_role->set_field("destination");

    goby::acomms::connect(&q_manager_.signal_receive, &received_data);
    goby::acomms::connect(&q_manager_.signal_ack, &received_ack);
    
    //
    // Initiate modem driver (libmodemdriver)
    //
    goby::acomms::protobuf::DriverConfig driver_cfg;
    driver_cfg.set_modem_id(my_id_);
    driver_cfg.set_serial_port(serial_port);
    
#ifdef USE_FLEXIBLE_DATA_PACKET
    driver_cfg.AddExtension(micromodem::protobuf::Config::nvram_cfg, "psk.packet.mod_hdr_version,1");
#endif

#ifdef USE_TWO_WAY_PING
    goby::acomms::connect(&mm_driver_.signal_receive, &monitor_modem_receive);
#endif

    //
    // Initiate medium access control (libamac)
    //
    goby::acomms::protobuf::MACConfig mac_cfg;
    mac_cfg.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);
    mac_cfg.set_modem_id(my_id_);
    goby::acomms::connect(&mac_.signal_initiate_transmission, &monitor_mac);
    
    goby::acomms::protobuf::ModemTransmission my_slot;
    my_slot.set_src(my_id_);
    my_slot.set_dest(buddy_id_);
#ifdef USE_FLEXIBLE_DATA_PACKET
    my_slot.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
    my_slot.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_FLEXIBLE_DATA);
    my_slot.set_max_frame_bytes(32);
    my_slot.set_rate(1);
#elif defined(USE_TWO_WAY_PING)
    my_slot.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
    my_slot.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
#else
    my_slot.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    my_slot.set_rate(0);
#endif
    my_slot.set_slot_seconds(12);
    
    goby::acomms::protobuf::ModemTransmission buddy_slot;
    buddy_slot.set_src(buddy_id_);
    buddy_slot.set_dest(my_id_);
    buddy_slot.set_slot_seconds(12);
#ifdef USE_FLEXIBLE_DATA_PACKET
    buddy_slot.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
    buddy_slot.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_FLEXIBLE_DATA);
    buddy_slot.set_max_frame_bytes(32);
    buddy_slot.set_rate(1);
#elif defined(USE_TWO_WAY_PING)
    buddy_slot.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
    buddy_slot.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
#else
    buddy_slot.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    buddy_slot.set_rate(0);
#endif

    if(my_id_ < buddy_id_)
    {
        mac_cfg.add_slot()->CopyFrom(my_slot);
        mac_cfg.add_slot()->CopyFrom(buddy_slot);
    }
    else
    {
        mac_cfg.add_slot()->CopyFrom(buddy_slot);
        mac_cfg.add_slot()->CopyFrom(my_slot);
    }    
    
    //
    // Start up everything
    //
    try
    {
        dccl_->set_cfg(dccl_cfg);
        q_manager_.set_cfg(q_manager_cfg);
        mac_.startup(mac_cfg);
        mm_driver_.startup(driver_cfg);
    }
    catch(std::runtime_error& e)
    {
        std::cerr << "exception at startup: " << e.what() << std::endl;
        return startup_failure();
    }
    
    curses_.set_modem_id(my_id_);    
    curses_.startup();
    
    //
    // Loop until terminated (CTRL-C)
    //
    for(;;)
    {
        std::string line;
        curses_.run_input(line);

        if(!line.empty())
        {
            ChatMessage message_out;
            message_out.set_telegram(line);
            
            // send this message to my buddy!
            message_out.set_destination(buddy_id_);
            message_out.set_source(my_id_);
            
            q_manager_.push_message(message_out);
        }
            
        try
        {
            mm_driver_.do_work();
            mac_.do_work();
            q_manager_.do_work();
        }
        catch(std::runtime_error& e)
        {
            curses_.cleanup();
            std::cerr << "exception while running: " << e.what() << std::endl;
            return 1;
        }
    }
    
    return 0;
}

int startup_failure()
{
    std::cerr << "usage: chat /dev/tty_modem my_id buddy_id log_file" << std::endl;
    return 1;
}

void monitor_mac(const goby::acomms::protobuf::ModemTransmission& mac_msg)
{
    if(mac_msg.src() == my_id_)
        curses_.post_message("{control} starting send to my buddy");
    else if(mac_msg.src() == buddy_id_)
        curses_.post_message("{control} my buddy might be sending to me now");
}

void monitor_modem_receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    if(rx_msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_TWO_WAY_PING &&
       rx_msg.HasExtension(micromodem::protobuf::ranging_reply))
    {
        const micromodem::protobuf::RangingReply& range_reply = rx_msg.GetExtension(micromodem::protobuf::ranging_reply);
        if(range_reply.one_way_travel_time_size() > 0)
        {
            double owtt = range_reply.one_way_travel_time(0);
            curses_.post_message(range_reply.ShortDebugString());
        }
        
    }
    
}

void received_data(const google::protobuf::Message& message_in)
{    
    ChatMessage typed_message_in;
    typed_message_in.CopyFrom(message_in);
    curses_.post_message(typed_message_in.source(), typed_message_in.telegram());
}

void received_ack(const goby::acomms::protobuf::ModemTransmission& ack_message,
                  const google::protobuf::Message& original_message)
{   
    ChatMessage typed_original_message;
    typed_original_message.CopyFrom(original_message);
    
    curses_.post_message(
        ack_message.src(),
        std::string("{ acknowledged receiving message starting with: "
                    + typed_original_message.telegram().substr(0,5) + " }"));
}
