// Copyright 2020-2025:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Thomas McCabe <tom.mccabe@missionsystems.com.au>
//   Jared Silbermann <jared.silbermann@missionsystems.com.au>
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

/************************************************************/
/*    NAME: Thomas McCabe                                   */
/*    ORGN: Mission Systems Pty Ltd                         */
/*    FILE: Popoto.cpp                                      */
/*    DATE: Aug 20 2020                                    */
/************************************************************/

/* Copyright (c) 2020 mission systems pty ltd */

#include "popoto_driver.h"

#include <cstdint>          // for uint8_t
#include <initializer_list> // for initializer...
#include <iostream>         // for operator<<
#include <list>             // for operator!=
#include <unistd.h>         // for usleep
#include <utility>          // for move

#include <boost/algorithm/string/trim.hpp> // for trim_copy
#include <boost/signals2/signal.hpp>       // for signal

#include "driver_exception.h" // for ModemDriver...
#include "driver_helpers.h"
#include "goby/acomms/acomms_constants.h"                // for BROADCAST_ID
#include "goby/acomms/protobuf/modem_driver_status.pb.h" // for ModemDriver...
#include "goby/exception.h"                              // for Exception
#include "goby/util/asio_compat.h"                       // for io_context
#include "goby/util/binary.h"                            // for hex_encode
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h" // for operator<<

//#include "popoto_client.hpp" // For popoto api (Ethernet driver)

using goby::glog;
using namespace goby::util::logger;
using json = nlohmann::json;

//void Popoto0PCMHandler(void* Pcm, int Len);
//popoto_client* popoto0 = NULL;
//volatile float Eng;
//volatile int pegCount = 0;
//volatile FILE* fpOut0 = NULL;
//volatile uint32_t EngCount;

goby::acomms::PopotoDriver::PopotoDriver() = default;
goby::acomms::PopotoDriver::~PopotoDriver() = default;

void goby::acomms::PopotoDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    if (!cfg.has_line_delimiter())
        driver_cfg_.set_line_delimiter("\r");

    // Popoto specific start up strings
    modem_power_ = popoto_driver_cfg().modem_power();
    int payload_mode = popoto_driver_cfg().payload_mode();
    int start_timeout = popoto_driver_cfg().start_timeout();
    application_type_ = popoto_driver_cfg().application_type();

    glog.is(DEBUG1) && glog << group(glog_out_group()) << "PopotoDriver: Starting modem..."
                            << std::endl;
    ModemDriverBase::modem_start(driver_cfg_);

    set_popoto_value("TxPowerWatts", modem_power_);
    set_popoto_value("PayloadMode", payload_mode);
    set_popoto_value("LedEnable", 0);
    set_popoto_value("LocalID", driver_cfg_.modem_id());

    // Get the modem temp and battery voltage
    get_popoto_value("BatteryVoltage");
    get_popoto_value("Temp_Ambient");

    // Check if modem has started
    std::string in;
    int startup_elapsed_ms = 0;

    while (!modem_read(&in))
    {
        usleep(100000); // 100 ms
        startup_elapsed_ms += 100;

        if (startup_elapsed_ms / 1000 >= start_timeout)
            throw(ModemDriverException("Modem physical connection failed to startup.",
                                       protobuf::ModemDriverStatus::STARTUP_FAILED));
    }

    while (!startup_done_)
    {
        do_work();
        usleep(100000); // 10 Hz
    }

    glog.is(DEBUG1) && glog << "Modem " << driver_cfg_.modem_id() << " initialized OK."
                            << std::endl;
}

void goby::acomms::PopotoDriver::shutdown() { ModemDriverBase::modem_close(); }

// --------------------------- Outgoing msgs ------------------------------------------------
void goby::acomms::PopotoDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    protobuf::ModemTransmission msg = orig_msg;
    // Poll the modem temp and battery voltage before each transmission
    get_popoto_value("BatteryVoltage");
    get_popoto_value("Temp_Ambient");

    std::stringstream raw;
    set_popoto_value("LocalID", driver_cfg_.modem_id());
    set_popoto_value("TxPowerWatts", modem_power_);

    switch (msg.type())
    {
        case protobuf::ModemTransmission::DATA:
        {
            msg.set_max_num_frames(1);

            if (!msg.has_max_frame_bytes())
                msg.set_max_frame_bytes(DEFAULT_MTU_BYTES);

            ModemDriverBase::signal_modify_transmission(&msg);

            msg.set_frame_start(0);

            if (msg.frame_size() == 0)
                ModemDriverBase::signal_data_request(&msg);

            next_frame_ += msg.frame_size();
            if (next_frame_ >= 255)
                next_frame_ = 0;

            if (msg.frame_size() > 0 && msg.frame(0).size() > 0)
            {
                glog.is(DEBUG1) && glog << group(glog_out_group())
                                        << "We were asked to transmit from " << msg.src() << " to "
                                        << msg.dest() << " at bitrate code " << msg.rate()
                                        << std::endl;
                glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending these data now: "
                                        << goby::util::hex_encode(msg.frame(0)) << std::endl;
                send(msg);
            }
            break;
        }

        case protobuf::ModemTransmission::DRIVER_SPECIFIC:
        {
            switch (msg.GetExtension(popoto::protobuf::transmission).type())
            {
                case popoto::protobuf::POPOTO_TWO_WAY_RANGE_REQUEST:
                    glog.is(DEBUG1) && glog << group(glog_out_group())
                                            << "We were asked to transmit a range request from "
                                            << msg.src() << " to " << msg.dest() << " at TX power "
                                            << modem_power_ << std::endl;
                    send_range_request(msg.dest());
                    break;

                case popoto::protobuf::POPOTO_PLAY_FILE: play_file(msg); break;

                case popoto::protobuf::POPOTO_TWO_WAY_PING:
                    glog.is(DEBUG1) && glog << group(glog_out_group())
                                            << "We were asked to send a two-way ping from "
                                            << msg.src() << " to " << msg.dest() << " at TX power "
                                            << modem_power_ << std::endl;
                    send_ping(msg);
                    break;

                case popoto::protobuf::POPOTO_DEEP_SLEEP: popoto_sleep(); break;

                case popoto::protobuf::POPOTO_SET_TX: popoto_update_power(msg); break;

                case popoto::protobuf::POPOTO_WAKE:
                    send_wake(); // the wake will just be a ping for the moment
                    break;
                case popoto::protobuf::POPOTO_TWO_WAY_RANGE_RESPONSE:
                    glog.is(WARN) &&
                        glog << group(glog_out_group())
                             << "You cannot send a "
                                " POPOTO_TWO_WAY_RANGE_RESPONSE. This is something that "
                                " you ONLY receive in return to a POPOTO_TWO_WAY_RANGE_REQUEST"
                             << std::endl;
                    break;

                default:
                    glog.is(DEBUG1) &&
                        glog << group(glog_out_group()) << warn
                             << "Not initiating transmission because we were given an invalid "
                                "DRIVER_SPECIFIC transmission type for the Popoto Modem"
                             << msg << std::endl;
                    break;
            }
            break; // Added the missing break -Supun-
        }

        default:
            glog.is(WARN) && glog << group(glog_out_group()) << "Unsupported transmission type: "
                                  << protobuf::ModemTransmission::TransmissionType_Name(msg.type())
                                  << std::endl;
            break;
    }
}
//--------------------------------------- send_wake ------------------------------------------------------------
// Send a wake command to the other modem, this can be any message so using a ping which can be changed if needed
void goby::acomms::PopotoDriver::send_wake(void) { send_popoto_command("ping", "1"); }
//--------------------------------------- popoto_sleep ---------------------------------------------------------
// Send a sleep command to the current modem
void goby::acomms::PopotoDriver::popoto_sleep(void)
{
    glog.is(DEBUG1) && glog << "Modem will now sleep: " << std::endl;
    send_popoto_command("powerdown");
}

void goby::acomms::PopotoDriver::popoto_update_power(protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG1) && glog << msg.DebugString() << std::endl;

    // Update in the popoto driver config so we don't overwrite it
    modem_power_ = msg.GetExtension(popoto::protobuf::transmission).transmit_power();
    set_popoto_value("TxPowerWatts", modem_power_);
}

//--------------------------------------- play_file ------------------------------------------------------------
// Play a file from the modem's directory
void goby::acomms::PopotoDriver::play_file(protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG1) && glog << msg.DebugString() << std::endl;

    // send over the wire
    send_popoto_command("playstop");
    send_popoto_command(
        "playstart",
        msg.GetExtension(popoto::protobuf::transmission).file_location() + " " +
            std::to_string(msg.GetExtension(popoto::protobuf::transmission).transmit_power()));
}
//--------------------------------------- send_ping ------------------------------------------------------------
// Send a ping
void goby::acomms::PopotoDriver::send_ping(protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG1) && glog << msg.DebugString() << std::endl;
    send_popoto_command(
        "ping", std::to_string(msg.GetExtension(popoto::protobuf::transmission).transmit_power()));
}

// ------------------------------------- Send -------------------------------------------------
void goby::acomms::PopotoDriver::send(protobuf::ModemTransmission& msg)
{
    // Set bitrate
    int min_rate = 0, max_rate = rate_to_speed.size() - 1;
    int rate = msg.rate();

    if (rate < min_rate || rate > max_rate)
    {
        glog.is(WARN) && glog << "Invalid rate, must be between " << min_rate << " and " << max_rate
                              << ". Using rate: " << min_rate << std::endl;
        rate = min_rate;
    }

    send_popoto_command(rate_to_speed[rate]);

    int dest = (msg.dest() == goby::acomms::BROADCAST_ID) ? POPOTO_BROADCAST_ID : msg.dest();
    set_popoto_value("RemoteID", dest);

    uint8_t header = CreateGobyHeader(msg);
    std::vector<std::uint8_t> bytes;
    bytes.push_back(header);
    if (msg.type() == protobuf::ModemTransmission::DATA)
    {
        signal_transmit_result(msg);
        bytes.insert(bytes.end(), msg.frame(0).begin(), msg.frame(0).end());
    }
    else if (msg.type() == protobuf::ModemTransmission::ACK)
    {
        // use empty data packet to indicate ACK
    }
    else
    {
        throw(goby::Exception(std::string("Unsupported type provided to send: ") +
                              protobuf::ModemTransmission::TransmissionType_Name(msg.type())));
    }

    json args;
    args["ClassUserID"] = 16;
    args["ApplicationType"] = application_type_;
    args["AckRequest"] = (msg.ack_requested() ? 1 : 0);
    args["StationID"] = driver_cfg_.modem_id();
    args["Payload"] = {{"Data", bytes}};
    send_popoto_command("TransmitJSON", args);
}

// ---------------------------- Ranging ----------------------------------------------------
void goby::acomms::PopotoDriver::send_range_request(int dest)
{
    set_popoto_value("RemoteID", dest);
    json args;
    args["TxPowerWatts"] = modem_power_;
    args["RemoteID"] = dest;
    send_popoto_command("Event_sendRanging", args);
}

// --------------------------- Incoming msgs ------------------------------------------------
void goby::acomms::PopotoDriver::do_work()
{
    std::string in;
    while (modem_read(&in))
    {
        try
        {
            // Remove VT100 sequences (if they exist) and popoto prompt
            in = StripString(in, "Popoto->");
            constexpr const char *VT100_BOLD_ON = "\x1b[1m", *VT100_BOLD_OFF = "\x1b[0m";

            in = StripString(in, VT100_BOLD_ON); // Below is over serial only
            in = StripString(in, VT100_BOLD_OFF);
            in = StripString(in, "MSMStatus "); // Below is over ethernet only
            in = StripString(in, "DataPacket ");
            in = StripString(in, "HeaderPacket ");
            in = StripString(in, "RangeReport ");

            protobuf::ModemRaw raw;
            raw.set_raw(in);
            ModemDriverBase::signal_raw_incoming(raw);
            if (json::accept(in))
            {
                ProcessJSON(in, modem_msg_);
                if (modem_msg_complete_)
                {
                    glog.is(DEBUG1) && glog << group(glog_in_group()) << "received: " << modem_msg_
                                            << std::endl;

                    if (modem_msg_.type() == protobuf::ModemTransmission::DATA &&
                        modem_msg_.ack_requested() && modem_msg_.dest() == driver_cfg_.modem_id())
                    {
                        // make any acks
                        protobuf::ModemTransmission ack;
                        ack.set_src(driver_cfg_.modem_id());
                        ack.set_dest(modem_msg_.src());

                        // make the acks at rate 0 for highest reliability
                        ack.set_rate(0);
                        ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
                        ack.set_frame_start(0);

                        send(ack); // reply with the ack msg
                    }
                    ModemDriverBase::signal_receive(modem_msg_);
                    modem_msg_.Clear();
                    modem_msg_complete_ = false;

                    transmissions_type_internal = UNKNOWN; // clearing this
                }
            }
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "Bad line: " << in << std::endl;
            glog.is(WARN) && glog << "Exception: " << e.what() << std::endl;
        }
    }
}
// --------------------------- Write over the wire ------------------------------------------------

void goby::acomms::PopotoDriver::send_popoto_command(const std::string& command,
                                                     const nlohmann::json& args)
{
    nlohmann::ordered_json j;
    j["Command"] = command;
    j["Arguments"] = args;
    std::string j_str = j.dump() + "\n";

    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(j_str);
    ModemDriverBase::signal_raw_outgoing(raw_msg);
    glog.is(DEBUG1) && glog << group(glog_out_group()) << boost::trim_copy(j_str) << std::endl;

    modem_write(j_str);
}

// Decode Popoto header
void goby::acomms::PopotoDriver::DecodeHeader(std::vector<uint8_t> data,
                                              protobuf::ModemTransmission& modem_msg)
{
    std::string type;

    // Popoto header types
    enum PopotoMessageType
    {
        DATA_MESSAGE = 0,
        RANGE_RESPONSE = 138,
        RANGE_REQUEST = 129,
        STATUS = 130
    };
    // Popoto documentation (https://www.popotomodem.com/static/17704245ba22b453f7e532e2d40273ee/PMM5544UsersGuide.pdf page 279) says range response MessageID is 128.
    // But looking at the actual range response messages, it is 138! -Supun-

    // Process binary payload data
    switch (data[0])
    {
        case DATA_MESSAGE:
            type = "Data message";
            {
                std::uint16_t payload_info{0};
                payload_info |= data[4] & 0xFF;
                payload_info |= (data[5] & 0xFF) << 8;

                // lowest 10 bits
                std::uint16_t length = payload_info & 0x3FF;
                // bit 10
                // bool streaming = payload_info & 0x400;
                // bits 11-15
                std::uint16_t modulation = (payload_info & 0xF800) >> 11;

                // use empty data packet to indicate ACK
                // TODO - get Popoto to provide ACK packet type in header
                if (length == 0)
                {
                    modem_msg.set_type(protobuf::ModemTransmission::ACK);
                }

                std::vector<int> modulation_to_rate{0, 4, 3, 2, 1, 5};
                if (modulation < modulation_to_rate.size())
                    modem_msg.set_rate(modulation_to_rate[modulation]);

                break;
            }

        case RANGE_RESPONSE:
            type = "Range response";
            modem_msg.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
            modem_msg.MutableExtension(popoto::protobuf::transmission)
                ->set_type(popoto::protobuf::POPOTO_TWO_WAY_RANGE_RESPONSE);
            break;

        case RANGE_REQUEST:
            type = "Range_request";
            modem_msg.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
            modem_msg.MutableExtension(popoto::protobuf::transmission)
                ->set_type(popoto::protobuf::POPOTO_TWO_WAY_RANGE_REQUEST);

            // all we get is a Header, no Data, so mark this message complete
            modem_msg_complete_ = true;

            break;

        case STATUS: type = "Status message"; break;

        default: glog.is(DEBUG1) && glog << "Unknown message type: " << data[0] << std::endl; break;
    }

    int sender = data[1];
    modem_msg.set_src(sender == POPOTO_BROADCAST_ID ? goby::acomms::BROADCAST_ID : sender);
    int receiver = data[2];
    modem_msg.set_dest(receiver == POPOTO_BROADCAST_ID ? goby::acomms::BROADCAST_ID : receiver);
    int tx_power = data[3];
    sender_id_ = sender;
    glog.is(DEBUG1) && glog << type << " from " << sender << " to " << receiver
                            << " at tx power: " << tx_power << std::endl;
}

// The only msg that is important for the dccl driver is the header and data. We will just print everything else to terminal for the moment
void goby::acomms::PopotoDriver::ProcessJSON(const std::string& message,
                                             protobuf::ModemTransmission& modem_msg)
{
    json j = json::parse(message);
    json::iterator it = j.begin();
    const std::string& label = it.key();
    // std::string str;

    protobuf::ModemRaw raw;
    raw.set_raw(message);
    if (label == "Header")
    {
        DecodeHeader(j["Header"], modem_msg);
    }
    else if (label == "Data")
    {
        std::string data = json_to_binary(j["Data"]);
        DecodeGobyHeader(data[0], modem_msg);
        if (modem_msg.type() == protobuf::ModemTransmission::DATA)
            *modem_msg.add_frame() = data.substr(1);

        // usually Data is the last message we receive
        modem_msg_complete_ = true;
    }
    else if (label == "Temp_Ambient")
    {
        glog.is(DEBUG1) && glog << "Temp_Ambient: " << j["Temp_Ambient"] << std::endl;

        if (!startup_done_)
        {
            // this is the last startup message we send
            glog.is(DEBUG1) && glog << "All startup configuration received" << std::endl;
            startup_done_ = true;
        }
    }
    else
    {
        glog.is(DEBUG1) && glog << label << ": " << j[label] << std::endl;
    }

    // Parsing the range report
    if (transmissions_type_internal == POPOTO_TWO_WAY_RANGE_RESPONSE)
    {
        if (j.contains("Range") && j.contains("Roundtrip Delay") && j.contains("SpeedOfSound"))
        {
            double range = j["Range"];
            double twtt = j["Roundtrip Delay"];
            double sound_speed = j["SpeedOfSound"];
            modem_msg.MutableExtension(popoto::protobuf::transmission)
                ->mutable_ranging_reply()
                ->set_one_way_travel_time(twtt / 2);
            modem_msg.MutableExtension(popoto::protobuf::transmission)
                ->mutable_ranging_reply()
                ->set_two_way_travel_time(twtt);
            modem_msg.MutableExtension(popoto::protobuf::transmission)
                ->mutable_ranging_reply()
                ->set_modem_range(range);
            modem_msg.MutableExtension(popoto::protobuf::transmission)
                ->mutable_ranging_reply()
                ->set_modem_sound_speed(sound_speed);
        }
    }
}

std::uint8_t goby::acomms::PopotoDriver::CreateGobyHeader(const protobuf::ModemTransmission& m)
{
    std::uint8_t header{0};
    if (m.type() == protobuf::ModemTransmission::DATA)
    {
        header |= (GOBY_DATA_TYPE & 0b11) << 6;

        // See if ack is requested, and encode it to the header.
        // This part was missing; thus, DecodeGobyHeader() was misbehaving..  -Supun-
        header &= ~(1 << GOBY_HEADER_ACK_REQUEST); // Clear bit 1 to set header to no-ack-request
        if (m.ack_requested())
        {
            header |= (1 << GOBY_HEADER_ACK_REQUEST); // Set bit 1 is ack is requested
        }
    }
    else if (m.type() == protobuf::ModemTransmission::ACK)
    {
        header |= (GOBY_ACK_TYPE & 0b11) << 6;
        header &= ~(1 << GOBY_HEADER_ACK_REQUEST); // Clear bit 1 to set header to no-ack-request
    }
    else
    {
        throw(goby::Exception(std::string("Unsupported type provided to CreateGobyHeader: ") +
                              protobuf::ModemTransmission::TransmissionType_Name(m.type())));
    }
    return header;
}

void goby::acomms::PopotoDriver::DecodeGobyHeader(std::uint8_t header,
                                                  protobuf::ModemTransmission& m)
{
    uint8_t goby_header_type = (header >> 6) & 0b11;
    switch (goby_header_type)
    {
        case GOBY_DATA_TYPE:
            m.set_type(protobuf::ModemTransmission::DATA);
            m.set_ack_requested(header & (1 << GOBY_HEADER_ACK_REQUEST));
            break;
        case GOBY_ACK_TYPE:
            m.set_type(protobuf::ModemTransmission::ACK);
            m.add_acked_frame(0);
            break;
    }
}
