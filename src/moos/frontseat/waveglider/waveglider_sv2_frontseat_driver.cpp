// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/common/logger.h"
#include "goby/util/as.h"

#include "waveglider_sv2_frontseat_driver.h"
#include <stdint.h>

namespace gpb = goby::moos::protobuf;
using goby::common::goby_time;
using goby::glog;
using namespace goby::common::logger;
using namespace goby::common::tcolor;

const int allowed_skew = 30;

// allows iFrontSeat to load our library
extern "C"
{
    FrontSeatInterfaceBase* frontseat_driver_load(iFrontSeatConfig* cfg)
    {
        return new WavegliderSV2FrontSeat(*cfg);
    }
}

uint16_t crc_compute_incrementally(uint16_t crc, char a);
uint16_t crc_compute(const std::string& buffer, unsigned offset, unsigned count, uint16_t seed);


WavegliderSV2FrontSeat::WavegliderSV2FrontSeat(const iFrontSeatConfig& cfg)
    : FrontSeatInterfaceBase(cfg),
      waveglider_sv2_config_(cfg.GetExtension(waveglider_sv2_config)),
      frontseat_providing_data_(false),
      last_frontseat_data_time_(0),
      frontseat_state_(gpb::FRONTSEAT_NOT_CONNECTED),
      serial_(goby::moos::SV2SerialConnection::create(io_, waveglider_sv2_config_.pm_serial_port(),
                                                      waveglider_sv2_config_.pm_serial_baud())),
      queued_messages_(1),
      dccl_("SV2.id", getenv("IFRONTSEAT_DRIVER_LIBRARY"))
{
    

    serial_->message_signal.connect(boost::bind(&WavegliderSV2FrontSeat::handle_sv2_message, this, _1));
    serial_->start();

    glog.is(VERBOSE) && glog << "Connected to WavegliderSV2 serial port." << std::endl;
    frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
}


void WavegliderSV2FrontSeat::loop()
{
    try 
    {
        io_.poll();    
    }
    catch(std::exception&e)
    {
        glog.is(WARN) && glog << "Failed to poll serial or process received data: " << e.what() << std::endl;
    }

    // if we haven't gotten data for a while, set this boolean so that the
    // FrontSeatInterfaceBase class knows
    if(goby_time<double>() > last_frontseat_data_time_ + allowed_skew)
        frontseat_providing_data_ = false;
}


void WavegliderSV2FrontSeat::send_command_to_frontseat(const gpb::CommandRequest& command)
{    
    if(command.has_desired_course())
    {
        dccl::uint32 board_addr = waveglider_sv2_config_.board_id() << dccl::BITS_IN_BYTE | waveglider_sv2_config_.task_id();

        boost::shared_ptr<goby::moos::protobuf::SV2CommandFollowFixedHeading> hdg_cmd(new goby::moos::protobuf::SV2CommandFollowFixedHeading);
        hdg_cmd->mutable_header()->set_start_of_frame(0x7e);
        hdg_cmd->mutable_header()->set_dest(goby::moos::protobuf::SV2Header::BOARD_ID_CC << dccl::BITS_IN_BYTE |
                                            goby::moos::protobuf::SV2Header::CCTASK_ID_COMMAND);
        hdg_cmd->mutable_header()->set_src(board_addr);
	hdg_cmd->mutable_header()->set_transaction_id(command.request_id());
        hdg_cmd->mutable_header()->set_message_type(goby::moos::protobuf::MESSAGE_TYPE_ACK);

        hdg_cmd->set_original_msg_type(goby::moos::protobuf::MESSAGE_TYPE_REQUEST_QUEUED_MESSAGE);
        hdg_cmd->set_command_format(0x0001);

        goby::moos::protobuf::SV2CommandFollowFixedHeading::CommandFollowFixedHeadingBody* body = hdg_cmd->mutable_body();
        
        body->set_level2id(0x0A);
        body->set_wgmsid(0xFFFFFFFF);
        body->set_data_size(18);
        body->set_structure_id(0x10);
        body->set_command_value(0x0008);
        body->set_reserved(0);
	body->set_heading_degrees(command.desired_course().heading());
        body->set_latitude(0);
        body->set_longitude(0);
        body->set_crc16(0); // will compute later

        std::string bytes;
        dccl_.encode(&bytes, *body);
        // remove the prefix created by DCCL and calculate CRC16
        bytes = bytes.substr(2);
        enum { CRC_SIZE = 2 };
        uint16_t calculated = crc_compute(bytes, 0, bytes.size()-CRC_SIZE, 0);
        body->set_crc16(calculated);
        
        hdg_cmd->mutable_footer()->set_crc16(0); // will compute later
        hdg_cmd->mutable_header()->set_message_size(dccl_.size(*hdg_cmd)-3); // doesn't include start of frame byte or extra 2-byte prefix

	glog.is(DEBUG1) && glog << "Queuing fixed heading cmd for heading of: " << command.desired_course().heading() << std::endl;
        glog.is(DEBUG2) && glog << hdg_cmd->DebugString() << std::endl;
        queued_messages_.push_back(hdg_cmd);
    }
    else
    {
        glog.is(VERBOSE) && glog << "Unhandled command: " << command.ShortDebugString() << std::endl;
    }
    
} 
    
void WavegliderSV2FrontSeat::send_data_to_frontseat(const gpb::FrontSeatInterfaceData& data)
{

}

void WavegliderSV2FrontSeat::send_raw_to_frontseat(const gpb::FrontSeatRaw& data)
{

} 

    
bool WavegliderSV2FrontSeat::frontseat_providing_data() const
{
    return frontseat_providing_data_;
} 

goby::moos::protobuf::FrontSeatState WavegliderSV2FrontSeat::frontseat_state() const
{
    return frontseat_state_;
} 


void WavegliderSV2FrontSeat::handle_sv2_message(const std::string& message)
{
    enum { MESSAGE_TYPE_START = 9, MESSAGE_TYPE_SIZE = 2 };

    // add prefix for DCCL id
    std::string bytes = message.substr(MESSAGE_TYPE_START, MESSAGE_TYPE_SIZE) + message; 
    bool ack_requested = !(bytes[1] & 0x80);
    glog.is(DEBUG2) && glog << (ack_requested ? "ACK Requested" : "No ACK Requested") << std::endl;
    bytes[1] &= 0x7F; // remove the ack requested bit;
    
    unsigned dccl_id = dccl_.id(bytes);    
    if(dccl_id == dccl_.id<goby::moos::protobuf::SV2RequestEnumerate>())
    {        
        goby::moos::protobuf::SV2RequestEnumerate enum_msg;
        dccl_.decode(bytes, &enum_msg);
        glog.is(DEBUG1) && glog << "Received enumeration request." << std::endl;
	glog.is(DEBUG2) && glog  << enum_msg.DebugString() << std::endl;
        check_crc(message, enum_msg.footer().crc16());
        handle_enumeration_request(enum_msg);
                
    }
    else if(dccl_id == dccl_.id<goby::moos::protobuf::SV2RequestStatus>())
    {
        goby::moos::protobuf::SV2RequestStatus request;
        dccl_.decode(bytes, &request);
        glog.is(DEBUG1) && glog << "Received status request." << std::endl;
	glog.is(DEBUG2) && glog << request.DebugString() << std::endl;
        frontseat_providing_data_ = true;
	last_frontseat_data_time_ = goby::common::goby_time<double>();
        handle_request_status(request);
    }    
    else if(dccl_id == dccl_.id<goby::moos::protobuf::SV2RequestQueuedMessage>())
    {
        goby::moos::protobuf::SV2RequestQueuedMessage request;
        dccl_.decode(bytes, &request);
        glog.is(DEBUG1) && glog << "Received queue message request. " << std::endl;
	glog.is(DEBUG2) && glog << request.DebugString() << std::endl;
        handle_request_queued_message(request);
    }    
    else if(dccl_id == dccl_.id<goby::moos::protobuf::SV2ACKNAKQueuedMessage>())
    {
        goby::moos::protobuf::SV2ACKNAKQueuedMessage ack;
        dccl_.decode(bytes, &ack);
        glog.is(DEBUG1) && glog << "Received queue message ack/nak." << std::endl;
	glog.is(DEBUG2) && glog << ack.DebugString() << std::endl;
        // HANDLE ACK QUEUED MESSAGE
    }
    else if(dccl_id == dccl_.id<goby::moos::protobuf::SV2GenericNAK>())
    {
        goby::moos::protobuf::SV2ACKNAKQueuedMessage nak;
        dccl_.decode(bytes, &nak);
        glog.is(DEBUG1) && glog << "Received generic nak." << std::endl;
	glog.is(DEBUG2) && glog << nak.DebugString() << std::endl;
        // HANDLE NAK
    }    
    else if(dccl_id == dccl_.id<goby::moos::protobuf::SV2GenericACK>())
    {
        goby::moos::protobuf::SV2GenericACK ack;
        dccl_.decode(bytes, &ack);
        glog.is(DEBUG1) && glog << "Received generic ack." << std::endl;
	glog.is(DEBUG2) && glog << ack.DebugString() << std::endl;
        // HANDLE ACK
    }
    else
    {
        glog.is(DEBUG1) && glog << "Received unhandled message type: " << std::hex << dccl_id << std::dec << std::endl;
    }
    
}

void WavegliderSV2FrontSeat::check_crc(const std::string& message, uint16_t expected)
{
    enum 
    {
        MAGIC_SIZE = 1,
        CRC_SIZE = 2
    };
    
    uint16_t calculated = crc_compute(message, MAGIC_SIZE, message.size() - MAGIC_SIZE - CRC_SIZE, 0);
    glog.is(DEBUG2) && glog << "Given CRC: " << std::hex << expected << ", computed: " << calculated << std::dec << std::endl;

    if(calculated != expected)
        glog.is(WARN) && glog << "Invalid CRC16" << std::endl;
}

void WavegliderSV2FrontSeat::add_crc(std::string* message)
{
    enum 
    {
        MAGIC_SIZE = 1,
        CRC_SIZE = 2
    };
    
    uint16_t calculated = crc_compute(*message, MAGIC_SIZE, message->size() - MAGIC_SIZE - CRC_SIZE, 0);
    message->at(message->size()-1) = (calculated >> dccl::BITS_IN_BYTE) & 0xff;
    message->at(message->size()-2) = (calculated) & 0xff;
    glog.is(DEBUG2) && glog << "Computed CRC: " << std::hex << calculated << std::dec << std::endl;
}



void WavegliderSV2FrontSeat::handle_enumeration_request(const goby::moos::protobuf::SV2RequestEnumerate& request)
{
    goby::moos::protobuf::SV2ReplyEnumerate reply;
    
    dccl::uint32 board_addr = waveglider_sv2_config_.board_id() << dccl::BITS_IN_BYTE | waveglider_sv2_config_.task_id();
    
    reply.mutable_header()->set_start_of_frame(0x7e);
    reply.mutable_header()->set_dest(goby::moos::protobuf::SV2Header::BOARD_ID_CC << dccl::BITS_IN_BYTE |
                                     goby::moos::protobuf::SV2Header::CCTASK_ID_MAIN);
    reply.mutable_header()->set_src(board_addr);
    reply.mutable_header()->set_transaction_id(request.header().transaction_id());
    reply.mutable_header()->set_message_type(goby::moos::protobuf::MESSAGE_TYPE_ACK);

    reply.set_original_msg_type(request.header().message_type());
    reply.set_number_of_devices_responding(1);
    reply.set_number_of_devices_in_message(1);
    reply.set_version(1);
    reply.set_device_type(0x1001);
    reply.set_board_addr(board_addr);
    reply.set_serial_number("000001");
    reply.set_location(0);
    reply.set_polling_frequency(1);

    enum { TELEMETRY = 0x01, POWER = 0x02, EVENT = 0x04, COMMAND_ACK_NAK = 0x08 };
    reply.set_extra_info(COMMAND_ACK_NAK);
    
    reply.set_firmware_major(0);
    reply.set_firmware_minor(0);
    reply.set_firmware_revision(1);

    std::string description("iFrontSeat Driver");
    reply.set_description(description + std::string(20-description.size(), '\0'));
    reply.mutable_footer()->set_crc16(0); // will compute later    

    reply.mutable_header()->set_message_size(dccl_.size(reply)-3); // doesn't include start of frame byte or extra 2-byte prefix

    glog.is(DEBUG1) && glog << "Sent enumeration reply." << std::endl;
    glog.is(DEBUG2) && glog << reply.DebugString() << std::endl;

    encode_and_write(reply);
}


void WavegliderSV2FrontSeat::handle_request_status(const goby::moos::protobuf::SV2RequestStatus& request)
{
    goby::moos::protobuf::SV2ReplyStatus reply;
    
    dccl::uint32 board_addr = waveglider_sv2_config_.board_id() << dccl::BITS_IN_BYTE | waveglider_sv2_config_.task_id();
    
    reply.mutable_header()->set_start_of_frame(0x7e);
    reply.mutable_header()->set_dest(goby::moos::protobuf::SV2Header::BOARD_ID_CC << dccl::BITS_IN_BYTE |
                                     goby::moos::protobuf::SV2Header::CCTASK_ID_MAIN);
    reply.mutable_header()->set_src(board_addr);
    reply.mutable_header()->set_transaction_id(request.header().transaction_id());
    reply.mutable_header()->set_message_type(goby::moos::protobuf::MESSAGE_TYPE_ACK);

    reply.set_original_msg_type(request.header().message_type());
    reply.set_number_of_devices_responding(1);
    reply.set_number_of_devices_in_message(1);

    bool queued_message_waiting = (queued_messages_.size() > 0);
    if(queued_message_waiting)
        reply.set_version(0x8001); // if queued messages available set bit 15 (manual says message type field?).
    else
        reply.set_version(0x0001);
    
    reply.set_board_addr(board_addr);

    reply.set_alarms(0);
    reply.set_leak_sensor_1(0);
    reply.set_leak_sensor_2(0);
    reply.set_humid_temp(0);
    reply.set_relative_humidity(0);
    reply.set_pressure_temp(0);
    reply.set_pressure(0);

    reply.mutable_footer()->set_crc16(0); // will compute later
    reply.mutable_header()->set_message_size(dccl_.size(reply)-3);
    glog.is(DEBUG1) && glog << "Sent status reply." << std::endl;
    glog.is(DEBUG2) && glog << reply.DebugString() << std::endl;

    encode_and_write(reply);
}


void WavegliderSV2FrontSeat::handle_request_queued_message(const goby::moos::protobuf::SV2RequestQueuedMessage& request)
{    

    if(queued_messages_.size())
    {
        boost::shared_ptr<goby::moos::protobuf::SV2CommandFollowFixedHeading> reply = queued_messages_.front();
	reply->mutable_header()->set_transaction_id(request.header().transaction_id());
        glog.is(DEBUG1) && glog << "Sent queued Message reply." << std::endl;
	glog.is(DEBUG2) && glog << reply->DebugString() << std::endl;
        encode_and_write(*reply);
        queued_messages_.pop_front();
    }
    else
    {
        glog.is(WARN) && glog << "No queued message to provide!" << std::endl;
    }
}


void WavegliderSV2FrontSeat::encode_and_write(const google::protobuf::Message& message)
{
    try 
    {
        std::string bytes;
        dccl_.encode(&bytes, message);
        // remove the prefix created by DCCL and calculate CRC16
        bytes = bytes.substr(2);
        add_crc(&bytes);
        glog.is(DEBUG2) && glog << "Sending encoded bytes (w/out escapes): " << dccl::hex_encode(bytes) << std::endl;
        serial_->write_start(bytes);
    }
    catch(std::exception& e)
    {
        glog.is(WARN) && glog << "Failed to encode and write message: " << e.what() << std::endl;
    }
}


uint16_t crc_compute(const std::string& buffer, unsigned offset, unsigned count, uint16_t seed)
{
    uint16_t crc = seed;
    for (unsigned idx = offset; count-- > 0; ++idx)
        crc = crc_compute_incrementally(crc, buffer[idx]);
    return crc;
}

uint16_t crc_compute_incrementally(uint16_t crc, char a)
{
    int i;
    crc ^= (a & 0xff);
    for (i = 0; i < 8; i++)
    {
        if ((crc & 1) == 1)
        {
            crc = (uint16_t)((crc >> 1) ^ 0xA001);
        }
        else
        {
            crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
