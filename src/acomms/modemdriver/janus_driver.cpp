// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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


goby::acomms::JanusDriver::JanusDriver()
{
    // other initialization you can do before you have your goby::acomms::DriverConfig configuration object
}

goby::acomms::JanusDriver::~JanusDriver()
{
    janus_parameters_free(params_tx);
    janus_parameters_free(params_rx);
    janus_simple_tx_free(simple_tx);
    janus_simple_rx_free(simple_rx);
}

janus_simple_tx_t goby::acomms::JanusDriver::init_janus_tx(){
    verbosity        = janus_driver_cfg().verbosity();
    pset_file        = janus_driver_cfg().pset_file();
    pset_id          = janus_driver_cfg().pset_id();
    class_id         = janus_driver_cfg().class_id();
    application_type = janus_driver_cfg().application_type();
    ack_request      = janus_driver_cfg().ack_request();
    params_tx->pset_id = pset_id;
    params_tx->pset_file = pset_file.c_str();
    params_tx->verbose = verbosity;
    params_tx->stream_driver = "alsa";
    params_tx->stream_driver_args = "default";// add config for this
    params_tx->stream_channel_count = janus_driver_cfg().channel_count();
    params_tx->stream_fs = janus_driver_cfg().sample_rate(); 
    params_tx->pad = 1; 
    simple_tx = janus_simple_tx_new(params_tx);

    if (!simple_tx){
      std::cerr << "ERROR: failed to initialize transmitter" << std::endl;;
      janus_parameters_free(params_tx);
    }   

    return simple_tx;
}

janus_parameters_t goby::acomms::JanusDriver::get_rx_params(){
    janus_parameters_t params = janus_parameters_new();
    params->verbose = 10; //verbosity;
    params->pset_id = 1;
    params->pset_file = "/usr/local/share/mig-moos-apps/iJanus/parameter_sets.csv";
    params->pset_center_freq = 0;
    params->pset_bandwidth = 0;
    params->chip_len_exp = 0;
    params->sequence_32_chips = JANUS_32_CHIP_SEQUENCE;

    // Stream parameters.
    params->stream_driver = "alsa";
    params->stream_driver_args = "dsnooped";
    params->stream_fs = 48000;
    params->stream_format = "S16";
    params->stream_passband = 1;
    params->stream_amp = JANUS_REAL_CONST(0.95);
    params->stream_mul = 1;
    params->stream_channel_count = 2;
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
}

janus_simple_rx_t goby::acomms::JanusDriver::init_janus_rx(){
    params_rx = get_rx_params();
    simple_rx = janus_simple_rx_new(params_rx);
    if (!simple_rx){
      std::cerr << "ERROR: failed to initialize receiver" << std::endl;
      exit(1);
      janus_parameters_free(params_tx);
    }      
    
    carrier_sensing = janus_carrier_sensing_new(janus_simple_rx_get_rx(simple_rx));
    packet_rx= janus_packet_new(params_rx->verbose);
    state_rx = janus_rx_state_new(params_rx);
    return simple_rx;
}

void goby::acomms::JanusDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;
    glog.is(DEBUG1) && glog << group(glog_out_group()) << "JanusDriver configuration good" << std::endl;
    ModemDriverBase::modem_start(driver_cfg_);
    simple_tx  = init_janus_tx();
    simple_rx = init_janus_rx();
} // startup

void goby::acomms::JanusDriver::shutdown()
{
    ModemDriverBase::modem_close();
} // shutdown

void goby::acomms::JanusDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
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
    if (next_frame_ >= 255)
        next_frame_ = 0;

    if (msg.frame_size() > 0 && msg.frame(0).size() > 0)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group())
                                << "We were asked to transmit from " << msg.src() << " to "
                                << msg.dest()  << std::endl;
        glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending these data now: "
                                << goby::util::hex_encode(msg.frame(0)) << std::endl;

        auto goby_header = CreateGobyHeader(msg);
        std::uint8_t header[2] =  {static_cast<std::uint8_t>(goby_header >> 8), 
                                   static_cast<std::uint8_t>(goby_header & 0xff)};
                                   
        glog.is(DEBUG1) && glog << "header bytes " << (int) header[0] << " " << (int) header[1] << std::endl;
        
        std::vector<std::uint8_t> payload(msg.frame(0).begin(), msg.frame(0).end());
        std::vector<std::uint8_t> message;
        message.push_back(header[0]);   
        message.push_back(header[1]);
        message.insert(message.end(), payload.begin(), payload.end());

        // calculate crc for 16-1. Note it must be the last 2 bytes so we pre pad and then append. This prevents
        // pAcommsHandler padding after the crc hsa been appended
        if(class_id == 16 && application_type == 1){
            // pad_message(message);
            std::uint16_t crc = janus_crc_16(message.data(),message.size(),0);
            message.push_back(static_cast<std::uint8_t>(crc >> 8));
            message.push_back(static_cast<std::uint8_t>(crc & 0xff));
        }
        int desired_cargo_size = message.size();
        janus_packet_t packet = janus_packet_new(verbosity);
        janus_packet_set_class_id(packet, class_id);
        janus_packet_set_app_type(packet, application_type);
        janus_packet_set_tx_rx(packet, 1);
        janus_packet_set_cargo(packet, message.data(),desired_cargo_size);
        
        janus_app_fields_t app_fields = janus_app_fields_new();
        janus_app_fields_add_field(app_fields, "StationIdentifier", std::to_string(msg.src()).c_str());
        janus_app_fields_add_field(app_fields, "DestinationIdentifier", std::to_string(msg.dest()).c_str());
        janus_app_fields_add_field(app_fields, "AckRequest", std::to_string(ack_request).c_str());
        janus_packet_set_application_data_fields(packet,app_fields);
        
        int cargo_error;
        cargo_error = janus_packet_set_cargo(packet, (janus_uint8_t*) message.data(), desired_cargo_size);
        if (cargo_error == JANUS_ERROR_CARGO_SIZE){
            fprintf(stderr, "ERROR: cargo size : %d exceeding maximum value\n", desired_cargo_size);
        }

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
    }
} // handle_initiate_transmission

// pads vector to multiple of 8 
void goby::acomms::JanusDriver::pad_message(std::vector<uint8_t> &vec) {
    if (vec.size() % 8 != 0) {
        int num_to_pad = 8 - (vec.size() % 8) - 2; // leave space for crc?
        if(vec.size() < 8 && vec.size() % 2 != 0){num_to_pad += 8;} // min padding size is  16
        vec.resize(vec.size() + num_to_pad, 0);
    }
}

// todo: convert to janus decode function
janus_rx_msg_pkt goby::acomms::JanusDriver::janus_packet_dump_cpp(const janus_packet_t pkt, bool verbosity){
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
            JANUS_DUMP("[iJanus] Packet", "Application Data Fields", "%s", "");
            JANUS_DUMP("[iJanus] Packet", "Cargo Size"  , "%u", cargo_size);   
            JANUS_DUMP("[iJanus] Packet", "CRC (8 bits)", "%u", janus_packet_get_crc(pkt));
            JANUS_DUMP("[iJanus] Packet", "CRC Validity", "%u", (janus_packet_get_validity(pkt) > 0));
        }

        char string_cargo[JANUS_MAX_PKT_CARGO_SIZE * 3 + 1];
        string_cargo[0] = '\0';
        for (i = 0; i < app_fields->field_count; ++i)
        {
            char name[64];
            sprintf(name, "  %s", app_fields->fields[i].name);
            if(verbosity) { JANUS_DUMP("[iJanus] Packet", name, "%s", app_fields->fields[i].value); }
            if (strcmp(PAYLOAD_LABEL, app_fields->fields[i].name) == 0) {
                packet_parsed.cargo = app_fields->fields[i].value;
            }
            else if (strcmp(PAYLOAD_SIZE_LABEL, app_fields->fields[i].name) == 0){
                packet_parsed.cargo_size = atoi(app_fields->fields[i].value);
            }
            else if (strcmp(STATION_ID_LABEL, app_fields->fields[i].name) == 0){
                packet_parsed.station_id = atoi(app_fields->fields[i].value);
            }
            else if (strcmp(DESTINATION_ID_LABEL, app_fields->fields[i].name) == 0){
                dest_set = true;
                packet_parsed.destination_id = atoi(app_fields->fields[i].value);
            }
            else if (strcmp(ACK_REQUEST_LABEL, app_fields->fields[i].name) == 0){
                packet_parsed.ack_request = *app_fields->fields[i].value != '0';
            }
        }

        for (i = 0; i <  packet_parsed.cargo_size; ++i){
            APPEND_FORMATTED(string_cargo, "%02X ", cargo[i]);
        }
        if(!dest_set){ packet_parsed.destination_id = -1; }
        if(verbosity) {JANUS_DUMP("[iJanus] Packet", "Cargo (hex)", "%s", string_cargo);}
        packet_parsed.cargo_hex = string_cargo;
        // return packet_parsed; -> just return at the end
    }
            
    // if(verbosity){
    std::cerr << "------ Got new message! ---------" << std::endl;
    std::cerr << "The SNR is: " +      std::to_string(state_rx->snr) << std::endl;
    std::cerr << "cargo_msg_size: "  + std::to_string(packet_parsed.cargo_size) << std::endl;
    std::cerr << "cargo_msg_hex: "   + packet_parsed.cargo_hex << std::endl;
    std::cerr << "cargo_msg cargo: " + packet_parsed.cargo << std::endl;
    std::cerr << "cargo_msg_src: "   + std::to_string(packet_parsed.station_id) << std::endl;
    std::cerr << "cargo_msg_dest: "  + std::to_string(packet_parsed.destination_id) << std::endl;
    // }

    return packet_parsed;
}

void goby::acomms::JanusDriver::to_modem_transmission(janus_rx_msg_pkt packet,protobuf::ModemTransmission& msg){
    // todo:  detect if this is an ack or a data message for now hard setting data
    msg.set_type(protobuf::ModemTransmission::DATA);
    msg.set_ack_requested(packet.ack_request);
    msg.set_src(packet.station_id);
    msg.set_dest(packet.destination_id);
    // msg.set_rate(); //-> todo: how to get this?
    msg.set_frame_start( 0 ); //todo: how to get this?
    std::string cargo_no_header = packet.cargo_hex.substr(6);
    std::string converted_cargo;
    for(int i = 0; i < cargo_no_header.size(); i+=3){
        std::string entry = cargo_no_header.substr(i,2);
        uint8_t value = std::stoi(entry, nullptr, 16);
        // Convert to char
        char character = static_cast<char>(value);
        converted_cargo += character;
    }
    msg.add_frame(converted_cargo); // test if converted form to chars (-2) works!
    ModemDriverBase::signal_receive(modem_msg);
    modem_msg.Clear();
}

void goby::acomms::JanusDriver::do_work()
{
    janus_rx_msg_pkt packet_parsed;
    std::string binary_msg;

    int retval = janus_rx_execute(janus_simple_rx_get_rx(simple_rx), packet_rx, state_rx);
    if (retval < 0){
        if (retval == JANUS_ERROR_OVERRUN){ std::cerr<< "Error: buffer-overrun" << std::endl; }
    } else if (retval > 0) {
        if (janus_packet_get_validity(packet_rx) && janus_packet_get_cargo_error(packet_rx) == 0){
            packet_parsed = janus_packet_dump_cpp(packet_rx,verbosity);

            // todo: fix src/dest check
            // if (acomms_id == packet_parsed.destination_id || packet_parsed.destination_id == -1){
            to_modem_transmission(packet_parsed,modem_msg);
            // } else {
                // std::cerr << "[iJanus] Ignoring msg because it is not meant for us." << std::endl;
            // }     
            janus_packet_reset(packet_rx);

        } else if (janus_packet_get_cargo_error(packet_rx) != 0){
            std::cerr <<  "[iJanus] Got a CRCError" << std::endl;
            // if (packet_parsed.ack_request){
                // std::cerr << "Unsuccfessfully decoded DCCL message. Playing nack.wav file to transmit a NACK" << std::endl;
                // if(nack_fpath != "none") { Notify(PLAY_WAV_MOOS_VAR, nack_fpath); } -> need to replace this functionality
            // }
            janus_packet_reset(packet_rx);
        }
        queried_detection_time = 0;
        janus_carrier_sensing_reset(carrier_sensing);
    } else {
        if (janus_simple_rx_has_detected(simple_rx) && !queried_detection_time ) {
            std::cerr << "Triggering detection (" + std::to_string(janus_simple_rx_get_first_detection_time(simple_rx)) + ")" << std::endl;
            queried_detection_time = 1;
        }
    }
    // send the packet over mooos to iJanus
} // do_work