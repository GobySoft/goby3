// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "moos_bluefin_driver.h"
#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/protobuf/mm_driver.pb.h"
#include "goby/common/logger.h"
#include "goby/moos/moos_protobuf_helpers.h"
#include "goby/moos/moos_string.h"
#include "goby/moos/protobuf/bluefin_driver.pb.h"
#include "goby/time.h"
#include "goby/util/binary.h"
#include "goby/util/linebasedcomms/nmea_sentence.h"
#include <boost/format.hpp>

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::common::logger;
using goby::acomms::operator<<;
using goby::common::goby_time;
using goby::common::nmea_time2ptime;
using goby::util::NMEASentence;

goby::moos::BluefinCommsDriver::BluefinCommsDriver(goby::acomms::MACManager* mac)
    : end_of_mac_window_(0), mac_(mac), last_request_id_(1 << 24)
{
}

void goby::moos::BluefinCommsDriver::startup(const goby::acomms::protobuf::DriverConfig& cfg)
{
    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Goby MOOS Bluefin Comms driver starting up." << std::endl;

    driver_cfg_ = cfg;

    modem_to_rate_to_bytes_.clear();
    for (int i = 0, n = driver_cfg_.ExtensionSize(protobuf::BluefinConfig::hardware_to_rate); i < n;
         ++i)
    {
        const protobuf::HardwareRatePair& hardware_to_rate =
            driver_cfg_.GetExtension(protobuf::BluefinConfig::hardware_to_rate, i);
        modem_to_rate_to_bytes_[hardware_to_rate.hardware_name()][hardware_to_rate.rate()] =
            hardware_to_rate.packet_bytes();
    }

    goby_to_bluefin_id_.clear();
    for (int i = 0, n = driver_cfg_.ExtensionSize(protobuf::BluefinConfig::modem_lookup); i < n;
         ++i)
    {
        const protobuf::BluefinModemIdLookUp& modem_lookup =
            driver_cfg_.GetExtension(protobuf::BluefinConfig::modem_lookup, i);
        goby_to_bluefin_id_.left.insert(
            std::make_pair(modem_lookup.goby_id(), modem_lookup.bluefin_id()));
    }

    const std::string& moos_server = driver_cfg_.GetExtension(protobuf::BluefinConfig::moos_server);
    int moos_port = driver_cfg_.GetExtension(protobuf::BluefinConfig::moos_port);
    moos_client_.Run(moos_server.c_str(), moos_port, "goby.moos.BluefinCommsDriver");

    int i = 0;
    while (!moos_client_.IsConnected())
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << "Trying to connect to MOOSDB at "
                                << moos_server << ":" << moos_port << ", try " << i++ << std::endl;
        sleep(1);
    }
    glog.is(DEBUG1) && glog << group(glog_out_group()) << "Connected to MOOSDB." << std::endl;

    moos_client_.Register(driver_cfg_.GetExtension(protobuf::BluefinConfig::nmea_in_moos_var), 0);
}

void goby::moos::BluefinCommsDriver::shutdown() { moos_client_.Close(); }

void goby::moos::BluefinCommsDriver::handle_initiate_transmission(
    const goby::acomms::protobuf::ModemTransmission& orig_msg)
{
    // copy so we can modify
    goby::acomms::protobuf::ModemTransmission msg = orig_msg;

    // allows zero to N third parties modify the transmission before sending.
    signal_modify_transmission(&msg);

    switch (msg.type())
    {
        case goby::acomms::protobuf::ModemTransmission::DATA:
        {
            if (driver_cfg_.modem_id() == msg.src())
            {
                // this is our transmission
                if (!modem_to_rate_to_bytes_.count(current_modem_))
                {
                    glog.is(WARN) &&
                        glog
                            << group(glog_out_group()) << "Modem \"" << current_modem_
                            << "\" not configured for rate and bytes. Cannot initiate transmission."
                            << std::endl;
                    break;
                }

                std::map<int, int>& rate_to_bytes = modem_to_rate_to_bytes_[current_modem_];

                if (!rate_to_bytes.count(msg.rate()))
                {
                    glog.is(WARN) && glog << group(glog_out_group()) << "Modem \"" << current_modem_
                                          << "\" not configured for rate: " << msg.rate()
                                          << ". Cannot initiate transmission." << std::endl;
                    break;
                }

                msg.set_max_frame_bytes(rate_to_bytes[msg.rate()]);

                // no data given to us, let's ask for some
                if (msg.frame_size() == 0)
                    ModemDriverBase::signal_data_request(&msg);

                // don't send an empty message
                if (msg.frame_size() && msg.frame(0).size())
                {
                    NMEASentence nmea("$BPCPD", NMEASentence::IGNORE);
                    nmea.push_back(unix_time2nmea_time(time::SystemClock::now().time_since_epoch() /
                                                       std::chrono::seconds(1)));
                    nmea.push_back(++last_request_id_);

                    int bf_dest = goby_to_bluefin_id_.left.count(msg.dest())
                                      ? goby_to_bluefin_id_.left.at(msg.dest())
                                      : -1;
                    nmea.push_back(bf_dest);
                    nmea.push_back(msg.rate());
                    nmea.push_back(static_cast<int>(msg.ack_requested()));
                    nmea.push_back(msg.frame_size());
                    for (int i = 0; i < 8; ++i)
                        nmea.push_back(i < msg.frame_size()
                                           ? boost::to_upper_copy(hex_encode(msg.frame(i)))
                                           : "");

                    std::stringstream ss;
                    ss << "@PB[lamss.protobuf.FrontSeatRaw] raw: \"" << nmea.message() << "\"";

                    goby::acomms::protobuf::ModemRaw out_raw;
                    out_raw.set_raw(ss.str());
                    ModemDriverBase::signal_raw_outgoing(out_raw);

                    const std::string& out_moos_var =
                        driver_cfg_.GetExtension(protobuf::BluefinConfig::nmea_out_moos_var);

                    glog.is(DEBUG1) && glog << group(glog_out_group()) << out_moos_var << ": "
                                            << ss.str() << std::endl;

                    moos_client_.Notify(out_moos_var, ss.str());
                    last_data_msg_ = msg;
                }
            }
        }
        break;

        case goby::acomms::protobuf::ModemTransmission::UNKNOWN:
        case goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC:
        case goby::acomms::protobuf::ModemTransmission::ACK:
            glog.is(DEBUG1) && glog << group(glog_out_group())
                                    << "Not initiating transmission: " << msg.ShortDebugString()
                                    << "; invalid transmission type." << std::endl;
            break;
    }
}

void goby::moos::BluefinCommsDriver::do_work()
{
    if (mac_ && mac_->running() &&
        end_of_mac_window_ < time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1))
        mac_->shutdown();

    MOOSMSG_LIST msgs;
    if (moos_client_.Fetch(msgs))
    {
        for (MOOSMSG_LIST::iterator it = msgs.begin(), end = msgs.end(); it != end; ++it)
        {
            const std::string& in_moos_var =
                driver_cfg_.GetExtension(protobuf::BluefinConfig::nmea_in_moos_var);
            const std::string& s_val = it->GetString();

            const std::string raw = "raw: \"";
            const std::string::size_type raw_pos = s_val.find(raw);
            if (raw_pos == std::string::npos)
                continue;

            const std::string::size_type end_pos = s_val.find("\"", raw_pos + raw.size());
            if (end_pos == std::string::npos)
                continue;

            std::string value =
                s_val.substr(raw_pos + raw.size(), end_pos - (raw_pos + raw.size()));

            if (it->GetKey() == in_moos_var &&
                (value.substr(0, 5) == "$BFCP" || value.substr(0, 6) == "$BFCMA"))
            {
                goby::acomms::protobuf::ModemRaw in_raw;
                in_raw.set_raw(s_val);
                ModemDriverBase::signal_raw_incoming(in_raw);
                try
                {
                    glog.is(DEBUG1) && glog << group(glog_in_group()) << in_moos_var << ": "
                                            << s_val << std::endl;
                    NMEASentence nmea(value, NMEASentence::VALIDATE);
                    if (nmea.sentence_id() == "CMA") // Communications Medium Access
                        bfcma(nmea);
                    else if (nmea.sentence_id() == "CPS") // Communications Packet Sent
                        bfcps(nmea);
                    else if (nmea.sentence_id() == "CPR") // Communications Packet Received Data
                        bfcpr(nmea);
                }
                catch (std::exception& e)
                {
                    glog.is(DEBUG1) && glog << warn << "Failed to handle message: " << e.what()
                                            << std::endl;
                }
            }
        }
    }
}

std::string goby::moos::BluefinCommsDriver::unix_time2nmea_time(double time)
{
    auto ptime = time::convert<boost::posix_time::ptime>(time * boost::units::si::seconds);

    // HHMMSS.SSS
    // it appears that exactly three digits of precision is important (sometimes)
    boost::format f("%02d%02d%02d.%03d");
    f % ptime.time_of_day().hours() % ptime.time_of_day().minutes() %
        ptime.time_of_day().seconds() %
        (ptime.time_of_day().fractional_seconds() * 1000 /
         boost::posix_time::time_duration::ticks_per_second());

    return f.str();
}

void goby::moos::BluefinCommsDriver::bfcma(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        END_OF_TIME_WINDOW = 2,
        MODEM_ADDRESS = 3,
        DEVICE_TYPE = 4
    };
    end_of_mac_window_ = time::convert<time::SITime>(nmea_time2ptime(nmea.at(END_OF_TIME_WINDOW))) /
                         boost::units::si::seconds;
    current_modem_ = nmea.at(DEVICE_TYPE);
    if (mac_)
        mac_->restart();
}

void goby::moos::BluefinCommsDriver::bfcps(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        ACOUSTIC_MESSAGE_TIMESTAMP = 2,
        REQUEST_ID = 3,
        NUMBER_OF_FRAMES = 4,
        FRAME_0_STATUS = 5,
        FRAME_1_STATUS = 6,
        FRAME_2_STATUS = 7,
        FRAME_3_STATUS = 8,
        FRAME_4_STATUS = 9,
        FRAME_5_STATUS = 10,
        FRAME_6_STATUS = 11,
        FRAME_7_STATUS = 12
    };

    if (nmea.as<int>(REQUEST_ID) == last_request_id_)
    {
        goby::acomms::protobuf::ModemTransmission msg;
        msg.set_time_with_units(time::SystemClock::now<time::MicroTime>());
        msg.set_src(last_data_msg_.dest()); // ack came from last data's destination, presumably
        msg.set_dest(last_data_msg_.src());
        msg.set_type(goby::acomms::protobuf::ModemTransmission::ACK);

        for (int i = 0, n = nmea.as<int>(NUMBER_OF_FRAMES); i < n; ++i)
        {
            if (nmea.as<int>(FRAME_0_STATUS + i) == 2)
                msg.add_acked_frame(i);
        }

        ModemDriverBase::signal_receive(msg);
    }
    else
    {
        glog.is(DEBUG1) &&
            glog << group(glog_in_group()) << warn
                 << "Received CPS for message ID that was not the last request sent, ignoring..."
                 << std::endl;
    }
}

void goby::moos::BluefinCommsDriver::bfcpr(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        ARRIVAL_TIME = 2,
        SOURCE = 3,
        DESTINATION = 4,
        TELEMETRY_MODE = 5,
        NUMBER_OF_FRAMES = 6,
        FRAME_0_STATUS = 7,
        FRAME_0_HEX = 8,
        FRAME_1_STATUS = 9,
        FRAME_1_HEX = 10,
        FRAME_2_STATUS = 11,
        FRAME_2_HEX = 12,
        FRAME_3_STATUS = 13,
        FRAME_3_HEX = 14,
        FRAME_4_STATUS = 15,
        FRAME_4_HEX = 16,
        FRAME_5_STATUS = 17,
        FRAME_5_HEX = 18,
        FRAME_6_STATUS = 19,
        FRAME_6_HEX = 20,
        FRAME_7_STATUS = 21,
        FRAME_7_HEX = 22
    };

    goby::acomms::protobuf::ModemTransmission msg;
    msg.set_time_with_units(time::convert<time::MicroTime>(nmea_time2ptime(nmea.at(ARRIVAL_TIME))));
    msg.set_time_source(goby::acomms::protobuf::ModemTransmission::MODEM_TIME);

    int goby_src = goby_to_bluefin_id_.right.count(nmea.as<int>(SOURCE))
                       ? goby_to_bluefin_id_.right.at(nmea.as<int>(SOURCE))
                       : -1;
    msg.set_src(goby_src);

    int goby_dest = goby_to_bluefin_id_.right.count(nmea.as<int>(DESTINATION))
                        ? goby_to_bluefin_id_.right.at(nmea.as<int>(DESTINATION))
                        : -1;

    msg.set_dest(goby_dest);
    msg.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    for (int i = 0, n = nmea.as<int>(NUMBER_OF_FRAMES); i < n; ++i)
    {
        if (nmea.as<int>(FRAME_0_STATUS + 2 * i) == 1)
            msg.add_frame(hex_decode(nmea.at(FRAME_0_HEX + 2 * i)));
    }

    glog.is(DEBUG1) && glog << group(glog_in_group())
                            << "Received BFCPR incoming data message: " << msg.DebugString()
                            << std::endl;

    ModemDriverBase::signal_receive(msg);
}
