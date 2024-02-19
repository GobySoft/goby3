// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Jared Silbermann <jared.silbermann@missionsystems.com.au>
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

#include "driver_helpers.h"

#include <cstdint>   // for int32_t
#include <exception> // for exception
#include <list>      // for operator!=
#include <ostream>   // for operator<<
#include <utility>   // for pair, make_pair
#include <vector>    // for vector

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF
#include <boost/algorithm/string/split.hpp>          // for split
#include <boost/algorithm/string/trim.hpp>           // for trim, trim_copy

#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmi...
#include "goby/time/system_clock.h"                // for SystemClock
#include "goby/time/types.h"                       // for MicroTime
#include "goby/util/as.h"                          // for as
#include "goby/util/binary.h"                      // for hex_decode
#include "goby/util/debug_logger.h"                // for glog
#include "goby/util/protobuf/io.h"                 // for operator<<
#include "goby/exception.h"                        // for Exception
#include "goby/time/convert.h"
#include "janus_driver.h"

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

#define PAYLOAD_LABEL "Payload"
#define PAYLOAD_SIZE_LABEL "PayloadSize"
#define ACK_REQUEST_LABEL "AckRequest"
#define STATION_ID_LABEL "StationIdentifier"
#define DESTINATION_ID_LABEL "DestinationIdentifier"
#define MIN_PACKET_SIZE_16_1 4 

goby::acomms::JanusDriver::JanusDriver()
{
}

goby::acomms::JanusDriver::~JanusDriver(){
    janus_parameters_free(params_tx);
    janus_parameters_free(params_rx);
    janus_simple_tx_free(simple_tx);
    janus_simple_rx_free(simple_rx);
}

janus_simple_tx_t goby::acomms::JanusDriver::init_janus_tx(){
    params_tx->pset_id = pset_id;
    params_tx->pset_file = pset_file.c_str();
    params_tx->verbose = verbosity;
    params_tx->stream_driver = "alsa";
    params_tx->stream_driver_args = tx_device.c_str();
    params_tx->stream_channel_count = tx_channels;
    params_tx->stream_fs = sample_rate; 
    params_tx->pad = 1; 
    simple_tx = janus_simple_tx_new(params_tx);

    if (!simple_tx){
      glog.is(DEBUG1) && glog << "ERROR: failed to initialize transmitter" << std::endl;;
      janus_parameters_free(params_tx);
    }   

    return simple_tx;
} // init janus tx

janus_parameters_t goby::acomms::JanusDriver::get_rx_params(){
    janus_parameters_t params = janus_parameters_new();
    params->verbose = verbosity;
    params->pset_id = 1;
    params->pset_file = pset_file.c_str();
    params->pset_center_freq = 0;
    params->pset_bandwidth = 0;
    params->chip_len_exp = 0;
    params->sequence_32_chips = JANUS_32_CHIP_SEQUENCE;

    // Stream parameters.
    params->stream_driver = "alsa";
    params->stream_driver_args = rx_device.c_str();
    params->stream_fs = sample_rate;
    params->stream_format = "S16";
    params->stream_passband = 1;
    params->stream_amp = JANUS_REAL_CONST(0.95);
    params->stream_mul = 1;
    params->stream_channel_count = rx_channels;
    params->stream_channel = 0;

    // Tx parameters.
    params->pad = 1;
    params->wut = 0;

    // Rx parameters.
    params->doppler_correction = 1;
    params->doppler_max_speed = JANUS_REAL_CONST(5.0);
    params->compute_channel_spectrogram = 1;
    params->detection_threshold = JANUS_REAL_CONST(2.5);
    params->colored_bit_prob = 0;
    params->cbp_high2medium  = JANUS_REAL_CONST(0.2);
    params->cbp_medium2low   = JANUS_REAL_CONST(0.35);
    
    return params;
} // get rx params

janus_simple_rx_t goby::acomms::JanusDriver::init_janus_rx(){
    params_rx = get_rx_params();
    simple_rx = janus_simple_rx_new(params_rx);
    if (!simple_rx){
      glog.is(DEBUG1) && glog << "ERROR: failed to initialize receiver" << std::endl;
      exit(1);
      janus_parameters_free(params_tx);
    }      
    
    carrier_sensing = janus_carrier_sensing_new(janus_simple_rx_get_rx(simple_rx));
    packet_rx= janus_packet_new(params_rx->verbose);
    state_rx = janus_rx_state_new(params_rx);
    return simple_rx;
} // init janus rx

void goby::acomms::JanusDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;
    glog.is(DEBUG1) && glog << group(glog_out_group()) << "JanusDriver configuration good" << std::endl;
    ModemDriverBase::modem_start(driver_cfg_);
    verbosity        = janus_driver_cfg().verbosity();
    pset_file        = janus_driver_cfg().pset_file();
    pset_id          = janus_driver_cfg().pset_id();
    class_id         = janus_driver_cfg().class_id();
    application_type = janus_driver_cfg().application_type();
    ack_request      = janus_driver_cfg().ack_request();
    tx_device        = janus_driver_cfg().tx_device();
    rx_device        = janus_driver_cfg().rx_device();
    tx_channels      = janus_driver_cfg().tx_channels();
    rx_channels      = janus_driver_cfg().rx_channels();
    sample_rate      = janus_driver_cfg().sample_rate();
    simple_tx        = init_janus_tx();
    simple_rx        = init_janus_rx();
} // startup

void goby::acomms::JanusDriver::shutdown()
{
    ModemDriverBase::modem_close();
} // shutdown

void goby::acomms::JanusDriver::append_crc16(std::vector<std::uint8_t> &vec){
    std::uint16_t crc = janus_crc_16(vec.data(),vec.size(),0);
    vec.push_back(static_cast<std::uint8_t>(crc >> 8));
    vec.push_back(static_cast<std::uint8_t>(crc & 0xff));
} // append crc16

void goby::acomms::JanusDriver::send_janus_packet(const protobuf::ModemTransmission& msg, std::vector<std::uint8_t> payload, bool ack){
    if(class_id == 16 && application_type == 1) { append_crc16(payload); } 
    int desired_cargo_size = payload.size();

    janus_packet_t packet = janus_packet_new(verbosity);
    janus_packet_set_class_id(packet, class_id);
    janus_packet_set_app_type(packet, application_type);
    janus_packet_set_tx_rx(packet, 1);
    
    janus_app_fields_t app_fields = janus_app_fields_new();
    janus_app_fields_add_field(app_fields, "StationIdentifier", std::to_string(msg.src()).c_str());
    janus_app_fields_add_field(app_fields, "DestinationIdentifier", std::to_string(msg.dest()).c_str());
    if(!ack) { janus_app_fields_add_field(app_fields, "AckRequest", std::to_string(ack_request).c_str()); } // todo: make sure default is zero
    janus_packet_set_application_data_fields(packet,app_fields);
    
    int cargo_error;
    cargo_error = janus_packet_set_cargo(packet, (janus_uint8_t*) payload.data(), desired_cargo_size);
    if (cargo_error == JANUS_ERROR_CARGO_SIZE)
        fprintf(stderr, "ERROR: cargo size : %d exceeding maximum value\n", desired_cargo_size);

    if (janus_packet_get_desired_cargo_size(packet)){
        janus_packet_encode_application_data(packet);
        janus_packet_set_validity(packet, 2);
    }

    janus_app_fields_free(app_fields);
    janus_tx_state_t state = janus_tx_state_new((params_tx->verbose > 1));
    int rv = janus_simple_tx_execute(simple_tx,packet,state);
    if (verbosity > 0){
        janus_tx_state_dump(state);
        janus_packet_dump(packet);
    }
    janus_tx_state_free(state);
    janus_packet_free(packet);
} // send_janus_packet 

void goby::acomms::JanusDriver::handle_initiate_transmission(const protobuf::ModemTransmission& orig_msg){
    // copy so we can modify
    protobuf::ModemTransmission msg = orig_msg;
    msg.set_max_num_frames(1);

    if (!msg.has_max_frame_bytes())
        msg.set_max_frame_bytes(DEFAULT_MTU_BYTES);

    ModemDriverBase::signal_modify_transmission(&msg);

    if (!msg.has_frame_start())
        msg.set_frame_start(next_frame_);

    if (msg.frame_size() == 0)
        ModemDriverBase::signal_data_request(&msg);

    next_frame_ += msg.frame_size();
    if (next_frame_ >= 63)
        next_frame_ = 0;

    if (msg.frame_size() > 0 && msg.frame(0).size() > 0){
        glog.is(DEBUG1) && glog << group(glog_out_group())
                                << "We were asked to transmit from " << msg.src() << " to "
                                << msg.dest()  << std::endl;
        glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending these data now: "
                                << goby::util::hex_encode(msg.frame(0)) << std::endl;

        std::vector<std::uint8_t> message = { get_goby_header(msg)}; 
        std::vector<std::uint8_t> payload(msg.frame(0).begin(), msg.frame(0).end());
        message.insert(message.end(), payload.begin(), payload.end());
        send_janus_packet(msg,message,false);
    }
} // handle_initiate_transmission

std::uint8_t goby::acomms::JanusDriver::get_goby_header(const protobuf::ModemTransmission& msg){
    std::uint8_t goby_header = CreateGobyHeader(msg);
    glog.is(DEBUG1) && glog << "header byte: " << (int) goby_header  << std::endl;
    return goby_header;
} // get_goby_header

void goby::acomms::JanusDriver::handle_ack_transmission(const protobuf::ModemTransmission& msg){
    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "We were asked to transmit ack from " << msg.src() << " to "
                            << msg.dest() << " for frame " << msg.acked_frame(0) << std::endl;
    std::vector<uint8_t> message = { get_goby_header(msg)}; 
    send_janus_packet(msg,message, true);
} // handle_ack_transmission

void goby::acomms::JanusDriver::send_ack(unsigned int src, unsigned int dest, unsigned int frame_number){
    protobuf::ModemTransmission ack;
    ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
    ack.set_src(dest);
    ack.set_dest(src);
    ack.set_rate(0);
    ack.set_frame_start(frame_number);
    ack.add_acked_frame(frame_number);
    handle_ack_transmission(ack);
} // send_ack

// Not currently used but may be useful to pad messages to multiples of 8 bytes
void goby::acomms::JanusDriver::pad_message(std::vector<uint8_t> &vec) {
    if (vec.size() % 8 != 0) {
        int num_to_pad = 8 - (vec.size() % 8) - 2; // leave space for crc?
        if(vec.size() < 8 && vec.size() % 2 != 0){num_to_pad += 8;} // min padding size is  16
        vec.resize(vec.size() + num_to_pad, 0);
    }
} // pad_message

janus_rx_msg_pkt goby::acomms::JanusDriver::parse_janus_packet(const janus_packet_t pkt, bool verbosity){
    unsigned i;
    unsigned int cargo_size;

    struct janus_rx_msg_pkt packet_parsed; 
    janus_app_fields_t app_fields = janus_app_fields_new();
    janus_packet_get_application_data_fields(pkt, app_fields);
    cargo_size = janus_packet_get_cargo_size(pkt);
    const janus_uint8_t* cargo = janus_packet_get_cargo(pkt);
    packet_parsed.cargo_size = cargo_size;
    bool dest_set = false;
    if (app_fields->field_count > 0) {
        if(verbosity){
            JANUS_DUMP("Packet", "Application Data Fields", "%s", "");
            JANUS_DUMP("Packet", "Cargo Size"  , "%u", cargo_size);   
            JANUS_DUMP("Packet", "CRC (8 bits)", "%u", janus_packet_get_crc(pkt));
            JANUS_DUMP("Packet", "CRC Validity", "%u", (janus_packet_get_validity(pkt) > 0));
        }

        char string_cargo[JANUS_MAX_PKT_CARGO_SIZE * 3 + 1];
        string_cargo[0] = '\0';
        for (i = 0; i < app_fields->field_count; ++i){
            char name[64];
            sprintf(name, "  %s", app_fields->fields[i].name);
            if(verbosity) { JANUS_DUMP("Packet", name, "%s", app_fields->fields[i].value); }
            // parse app fields
            if (strcmp(PAYLOAD_LABEL, app_fields->fields[i].name) == 0) 
                packet_parsed.cargo = app_fields->fields[i].value;
            else if (strcmp(PAYLOAD_SIZE_LABEL, app_fields->fields[i].name) == 0)
                packet_parsed.cargo_size = atoi(app_fields->fields[i].value);
            else if (strcmp(STATION_ID_LABEL, app_fields->fields[i].name) == 0)
                packet_parsed.station_id = atoi(app_fields->fields[i].value);
            else if (strcmp(DESTINATION_ID_LABEL, app_fields->fields[i].name) == 0){
                dest_set = true;
                packet_parsed.destination_id = atoi(app_fields->fields[i].value);
            } else if (strcmp(ACK_REQUEST_LABEL, app_fields->fields[i].name) == 0)
                packet_parsed.ack_request = *app_fields->fields[i].value != '0';
        }

        for (i = 0; i <  packet_parsed.cargo_size; ++i){ APPEND_FORMATTED(string_cargo, "%02X ", cargo[i]);}
        if(!dest_set){ packet_parsed.destination_id = -1; }
        if(verbosity) {JANUS_DUMP("Packet", "Cargo (hex)", "%s", string_cargo);}
        packet_parsed.cargo_hex = string_cargo;
    }
            
    if(verbosity){
        glog.is(DEBUG1) && glog << "------ Got new message! ---------" << std::endl;
        glog.is(DEBUG1) && glog << "The SNR is: " +      std::to_string(state_rx->snr) << std::endl;
        glog.is(DEBUG1) && glog << "cargo_msg_size: "  + std::to_string(packet_parsed.cargo_size) << std::endl;
        glog.is(DEBUG1) && glog << "cargo_msg_hex: "   + packet_parsed.cargo_hex << std::endl;
        glog.is(DEBUG1) && glog << "cargo_msg cargo: " + packet_parsed.cargo << std::endl;
        glog.is(DEBUG1) && glog << "cargo_msg_src: "   + std::to_string(packet_parsed.station_id) << std::endl;
        glog.is(DEBUG1) && glog << "cargo_msg_dest: "  + std::to_string(packet_parsed.destination_id) << std::endl;
    }

    return packet_parsed;
}

unsigned int goby::acomms::JanusDriver::get_frame_num(std::string header){
    int frame_number =  std::stoi(header, nullptr, 16) & 0b00111111;
    return frame_number;
}

void goby::acomms::JanusDriver::to_modem_transmission(janus_rx_msg_pkt packet,protobuf::ModemTransmission& msg){
    msg.set_src(packet.station_id);
    msg.set_dest(packet.destination_id);
    msg.set_rate(0);
    uint8_t goby_header = std::stoi(packet.cargo_hex.substr(0,2), nullptr, 16); // first byte is goby header
    DecodeGobyHeader(goby_header,msg);
    if(msg.type() == protobuf::ModemTransmission::DATA){
        msg.set_ack_requested(packet.ack_request);
        std::string cargo_no_header = packet.cargo_hex.substr(3);
        std::string converted_cargo;
        for(int i = 0; i < cargo_no_header.size(); i+=3){
            std::string entry = cargo_no_header.substr(i,2);
            uint8_t value = std::stoi(entry, nullptr, 16);
            char character = static_cast<char>(value);
            converted_cargo += character;
        }
        msg.add_frame(converted_cargo); 
    } 
}

void goby::acomms::JanusDriver::do_work(){
    janus_rx_msg_pkt packet_parsed;
    std::string binary_msg;

    int retval = janus_rx_execute(janus_simple_rx_get_rx(simple_rx), packet_rx, state_rx);
    if (retval < 0){
        if (retval == JANUS_ERROR_OVERRUN){ glog.is(DEBUG1) && glog<< "Error: buffer-overrun" << std::endl; }
    } else if (retval > 0) {
        if (janus_packet_get_validity(packet_rx) && janus_packet_get_cargo_error(packet_rx) == 0){
            packet_parsed = parse_janus_packet(packet_rx,verbosity);
            int frame_number;
            if (packet_parsed.cargo_size > 0){
                if (driver_cfg_.modem_id() == packet_parsed.destination_id || packet_parsed.destination_id == -1){
                    to_modem_transmission(packet_parsed,modem_msg);
                    ModemDriverBase::signal_receive(modem_msg);
                } else {
                    glog.is(DEBUG1) && glog << "Ignoring msg because it is not meant for us." << std::endl;
                } 
                if(packet_parsed.ack_request && (class_id == 16 && application_type == 1) ) // acks only supported for 16-1 since we require a destination id
                    send_ack(packet_parsed.station_id, packet_parsed.destination_id, modem_msg.frame_start());
            } else{
                glog.is(DEBUG1) && glog << "Recieved message with no cargo" << std::endl;
            }
            modem_msg.Clear();
            janus_packet_reset(packet_rx);
        } else if (janus_packet_get_cargo_error(packet_rx) != 0){
            glog.is(DEBUG1) && glog <<  "Got a CRCError" << std::endl;
            janus_packet_reset(packet_rx);
        }
        queried_detection_time = 0;
        janus_carrier_sensing_reset(carrier_sensing);
    } else {
        if (janus_simple_rx_has_detected(simple_rx) && !queried_detection_time ) {
            glog.is(DEBUG1) && glog << "Triggering detection (" + std::to_string(janus_simple_rx_get_first_detection_time(simple_rx)) + ")" << std::endl;
            queried_detection_time = 1;
        }
    }
} // do_work

// Format is 2 bits for type, 6 bits for frame counter
std::uint8_t goby::acomms::JanusDriver::CreateGobyHeader(const protobuf::ModemTransmission& m){
    std::uint8_t header{0};
    if (m.type() == protobuf::ModemTransmission::DATA){
        header |= ( GOBY_DATA_TYPE & 0b11 ) << 6;
        header |= ( m.frame_start() & 0b00111111 );
    } else if (m.type() == protobuf::ModemTransmission::ACK){
        header |= ( GOBY_ACK_TYPE & 0b11 ) << 6;
        header |= ( m.frame_start() & 0b00111111 );
    } else {
        throw(goby::Exception(std::string("Unsupported type provided to CreateGobyHeader: ") +
                              protobuf::ModemTransmission::TransmissionType_Name(m.type())));
    }
    return header;
} // CreateGobyHeader

void goby::acomms::JanusDriver::DecodeGobyHeader(std::uint8_t header, protobuf::ModemTransmission& m){
    int frame_number = header & 0b00111111;
    m.set_type((( (header >> 6) & 0b11 ) == GOBY_ACK_TYPE ) ? protobuf::ModemTransmission::ACK: protobuf::ModemTransmission::DATA);
    if (m.type() == protobuf::ModemTransmission::DATA)
        m.set_frame_start( frame_number );
    else if (m.type() == protobuf::ModemTransmission::ACK){
        m.set_frame_start(frame_number);
        m.add_acked_frame( frame_number );
    }
} // DecodeGobyHeader
