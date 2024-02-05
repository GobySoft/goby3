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

#include "janus_driver.h"
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
using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

goby::acomms::JanusDriver::JanusDriver()
{
    // other initialization you can do before you have your goby::acomms::DriverConfig configuration object
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
    
    // send empty transmission to get the modem to start properly
    handle_initiate_transmission(protobuf::ModemTransmission());    
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
        message.insert(message.end(), header[0], header[1]);
        message.insert(message.end(), payload.begin(), payload.end());
        
        // calculate crc for class_id 16 and application_type 1
        if(class_id == 16 && application_type == 1){
            std::uint16_t crc = calculate_crc_16(message.data(),message.size(),0);
            std::vector<std::uint8_t> crc_bytes= {static_cast<std::uint8_t>(crc >> 8), static_cast<std::uint8_t>(crc & 0xff)};
            message.insert(message.end(), crc_bytes.begin(), crc_bytes.end());
        }

        // Binary vector to string 
        std::string binary_str(reinterpret_cast<const char*>(message.data()), message.size());
        std::string janus_tx_command = "janus-tx --pset-file " + pset_file + " --pset-id " + std::to_string(pset_id) + " --stream-driver alsa --stream-driver-args default --packet-class-id " 
            + std::to_string(class_id) + " --packet-app-type " + std::to_string(application_type) + " --packet-app-fields 'StationIdentifier=" + std::to_string(msg.src()) 
            + ",AckRequest=" + std::to_string(ack_request) + ",DestinationIdentifier=" + std::to_string(msg.dest()) + "' --packet-cargo " + binary_str;
        
        int result = system(janus_tx_command.c_str()); 
        if(result != 0 ){ 
            std::cerr << "INVALID: " + janus_tx_command << std::endl; 
            std::__throw_runtime_error("janus-tx command may be missing! Please install janus library and try again!");    
        }
    
    }
} // handle_initiate_transmission

std::uint16_t goby::acomms::JanusDriver::calculate_crc_16(std::uint8_t* data,unsigned data_len, std::uint16_t crc)
{
    while (data_len-- > 0){
        crc = (crc >> 8) ^ c_crc16_ibm_table[(crc ^ *data++) & 0xff];
    }
    return crc;
}

 // calculate_crc_16
// Recieving messages with janus modem not currently supported: Coming Soon!
void goby::acomms::JanusDriver::do_work()
{

} // do_work

