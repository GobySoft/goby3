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

goby::acomms::JanusDriver::JanusDriver()
{
    // other initialization you can do before you have your goby::acomms::DriverConfig configuration object
}

goby::acomms::JanusDriver::~JanusDriver()
{
    janus_parameters_free(params);
    janus_simple_tx_free(tx);
}

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

    params->pset_id = pset_id;
    params->pset_file = pset_file.c_str();
    params->verbose = verbosity;
    params->stream_driver = "alsa";
    params->stream_driver_args = "default";
    params->stream_channel_count = janus_driver_cfg().channel_count();
    params->stream_fs = janus_driver_cfg().sample_rate(); 
    params->pad = 1; 
    tx = janus_simple_tx_new(params);
    
    if (!tx){
      std::cerr << "ERROR: failed to initialize transmitter" << std::endl;;
      janus_parameters_free(params);
    }
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
        janus_tx_state_t state = janus_tx_state_new((params->verbose > 1));
        int rv = janus_simple_tx_execute(tx,packet,state);
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

// Recieving messages with janus modem not currently supported: Coming Soon!
void goby::acomms::JanusDriver::do_work()
{

} // do_work

