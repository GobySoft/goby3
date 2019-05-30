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

#include "goby/util/debug_logger.h"

#include "bluefin.h"

namespace gpb = goby::moos::protobuf;
namespace gtime = goby::time;

using goby::glog;
using goby::common::goby_time;
using goby::util::NMEASentence;
using namespace goby::common::logger;
using namespace goby::common::tcolor;

void BluefinFrontSeat::bfack(const goby::util::NMEASentence& nmea)
{
    frontseat_providing_data_ = true;
    last_frontseat_data_time_ = gtime::SystemClock::now();

    enum
    {
        TIMESTAMP = 1,
        COMMAND_NAME = 2,
        TIMESTAMP_OF_COMMAND = 3,
        BEHAVIOR_INSERT_ID = 4,
        ACK_STATUS = 5,
        FUTURE_USE = 6,
        DESCRIPTION = 7,
    };

    enum AckStatus
    {
        INVALID_REQUEST = 0,
        REQUEST_UNSUCCESSFULLY_PROCESSED = 1,
        REQUEST_SUCCESSFULLY_PROCESSED = 2,
        REQUEST_PENDING = 3
    };

    AckStatus status = static_cast<AckStatus>(nmea.as<int>(ACK_STATUS));

    std::string acked_sentence = nmea.at(COMMAND_NAME);

    switch (status)
    {
        case INVALID_REQUEST:
            glog.is(DEBUG1) && glog << warn << "Huxley reports that we sent an invalid "
                                    << acked_sentence << " request." << std::endl;
            break;
        case REQUEST_UNSUCCESSFULLY_PROCESSED:
            glog.is(DEBUG1) && glog << warn
                                    << "Huxley reports that it unsuccessfully processed our "
                                    << acked_sentence << " request: "
                                    << "\"" << nmea.at(DESCRIPTION) << "\"" << std::endl;
            break;
        case REQUEST_SUCCESSFULLY_PROCESSED: break;
        case REQUEST_PENDING:
            glog.is(DEBUG1) && glog << "Huxley reports that our " << acked_sentence
                                    << " request is pending." << std::endl;
            if (!out_.empty())
                pending_.push_back(out_.front().message());
            break;
    }

    boost::to_upper(acked_sentence);
    // we expect it to be the front of either the out_ or pending_ queues
    if (!out_.empty() && boost::iequals(out_.front().sentence_id(), acked_sentence))
    {
        out_.pop_front();
    }
    else if (!pending_.empty() && boost::iequals(pending_.front().sentence_id(), acked_sentence))
    {
        pending_.pop_front();
    }
    else
    {
        glog.is(DEBUG1) && glog << warn << "Received NMEA Ack for message that was the front "
                                << "of neither the outgoing or pending queue. Clearing our queues "
                                   "and attempting to carry on ..."
                                << std::endl;
        out_.clear();
        pending_.clear();
        return;
    }

    // handle response to outstanding requests
    if (status != REQUEST_PENDING)
    {
        gpb::BluefinExtraCommands::BluefinCommand type = gpb::BluefinExtraCommands::UNKNOWN_COMMAND;
        switch (sentence_id_map_.left.at(acked_sentence))
        {
            case RMB: type = gpb::BluefinExtraCommands::DESIRED_COURSE; break;
            case BOY: type = gpb::BluefinExtraCommands::BUOYANCY_ADJUST; break;
            case TRM: type = gpb::BluefinExtraCommands::TRIM_ADJUST; break;
            case SIL: type = gpb::BluefinExtraCommands::SILENT_MODE; break;
            case RCB: type = gpb::BluefinExtraCommands::CANCEL_CURRENT_BEHAVIOR; break;
            default: break;
        }

        if (outstanding_requests_.count(type))
        {
            gpb::CommandResponse response;
            response.set_request_successful(status == REQUEST_SUCCESSFULLY_PROCESSED);
            response.set_request_id(outstanding_requests_[type].request_id());
            if (!response.request_successful())
            {
                response.set_error_code(status);
                response.set_error_string(nmea.at(DESCRIPTION));
            }
            outstanding_requests_.erase(type);
            signal_command_response(response);
        }
    }
    waiting_for_huxley_ = false;
}

void BluefinFrontSeat::bfmsc(const goby::util::NMEASentence& nmea)
{
    // TODO: See if there is something to the message contents
    // BF manual says: Arbitrary textual message. Semantics determined by the payload.
    if (bf_config_.accepting_commands_hook() == BluefinFrontSeatConfig::BFMSC_TRIGGER)
        frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
}

void BluefinFrontSeat::bfnvg(const goby::util::NMEASentence& nmea)
{
    frontseat_providing_data_ = true;
    last_frontseat_data_time_ = gtime::SystemClock::now();

    enum
    {
        TIMESTAMP = 1,
        LATITUDE = 2,
        LAT_HEMISPHERE = 3,
        LONGITUDE = 4,
        LON_HEMISPHERE = 5,
        QUALITY_OF_POSITION = 6,
        ALTITUDE = 7,
        DEPTH = 8,
        HEADING = 9,
        ROLL = 10,
        PITCH = 11,
        COMPUTED_TIMESTAMP = 12
    };

    // parse out the message
    status_.Clear(); // NVG clears the message, NVR sends it
    status_.set_time_with_units(gtime::convert<gtime::MicroTime>(
        goby::common::nmea_time2ptime(nmea.at(COMPUTED_TIMESTAMP))));

    const std::string& lat_string = nmea.at(LATITUDE);
    if (lat_string.length() > 2)
    {
        double lat_deg = goby::util::as<double>(lat_string.substr(0, 2));
        double lat_min = goby::util::as<double>(lat_string.substr(2, lat_string.size()));
        double lat = lat_deg + lat_min / 60;
        status_.mutable_global_fix()->set_lat((nmea.at(LAT_HEMISPHERE) == "S") ? -lat : lat);
    }
    else
    {
        status_.mutable_global_fix()->set_lat(std::numeric_limits<double>::quiet_NaN());
    }

    const std::string& lon_string = nmea.at(LONGITUDE);
    if (lon_string.length() > 2)
    {
        double lon_deg = goby::util::as<double>(lon_string.substr(0, 3));
        double lon_min = goby::util::as<double>(lon_string.substr(3, nmea.at(4).size()));
        double lon = lon_deg + lon_min / 60;
        status_.mutable_global_fix()->set_lon((nmea.at(LON_HEMISPHERE) == "W") ? -lon : lon);
    }
    else
    {
        status_.mutable_global_fix()->set_lon(std::numeric_limits<double>::quiet_NaN());
    }

    if (nmea.as<int>(QUALITY_OF_POSITION) == 1)
    {
        status_.mutable_global_fix()->set_lat_source(goby::moos::protobuf::GPS);
        status_.mutable_global_fix()->set_lon_source(goby::moos::protobuf::GPS);
    }

    status_.mutable_global_fix()->set_altitude(nmea.as<double>(ALTITUDE));
    status_.mutable_global_fix()->set_depth(nmea.as<double>(DEPTH));
    status_.mutable_pose()->set_heading(nmea.as<double>(HEADING));
    status_.mutable_pose()->set_roll(nmea.as<double>(ROLL));
    status_.mutable_pose()->set_pitch(nmea.as<double>(PITCH));
}

void BluefinFrontSeat::bfnvr(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        EAST_VELOCITY = 2,
        NORTH_VELOCITY = 3,
        DOWN_VELOCITY = 4,
        PITCH_RATE = 5,
        ROLL_RATE = 6,
        YAW_RATE = 7,
    };

    auto status_time = status_.time_with_units();
    auto dt =
        gtime::convert<decltype(status_time)>(goby::common::nmea_time2ptime(nmea.at(TIMESTAMP))) -
        status_time;

    double east_speed = nmea.as<double>(EAST_VELOCITY);
    double north_speed = nmea.as<double>(NORTH_VELOCITY);

    status_.mutable_pose()->set_pitch_rate(nmea.as<double>(PITCH_RATE));
    status_.mutable_pose()->set_roll_rate(nmea.as<double>(ROLL_RATE));
    status_.mutable_pose()->set_heading_rate(nmea.as<double>(YAW_RATE));
    status_.set_speed(std::sqrt(north_speed * north_speed + east_speed * east_speed));

    status_.mutable_pose()->set_roll_rate_time_lag_with_units(dt);
    status_.mutable_pose()->set_pitch_rate_time_lag_with_units(dt);
    status_.mutable_pose()->set_heading_rate_time_lag_with_units(dt);
    status_.set_speed_time_lag_with_units(dt);

    // fill in the local X, Y
    compute_missing(&status_);

    gpb::FrontSeatInterfaceData data;
    data.mutable_node_status()->CopyFrom(status_);
    signal_data_from_frontseat(data);
}

void BluefinFrontSeat::bfsvs(const goby::util::NMEASentence& nmea)
{
    // If the Bluefin vehicle is equipped with a sound velocity sensor, this message will provide the raw output of that sensor. If not, then an estimated value will be provided.

    // We don't use this, choosing to calculate it ourselves from the CTD
}

void BluefinFrontSeat::bfrvl(const goby::util::NMEASentence& nmea)
{
    // Vehicle velocity through water as estimated from thruster RPM, may be empty if no lookup table is implemented (m/s)
}

void BluefinFrontSeat::bfsht(const goby::util::NMEASentence& nmea)
{
    glog.is(WARN) && glog << "Bluefin sent us the SHT message: they are shutting down!"
                          << std::endl;
}

void BluefinFrontSeat::bfmbs(const goby::util::NMEASentence& nmea)
{
    // This message is sent when the Bluefin vehicle is just beginning a new behavior in the current mission. It can be used by payloads for record-keeping or to synchronize actions with the current mission. Use of the (d--d) dive file field is considered deprecated in favor of getting the same information from BFMIS. See also the BFPLN message below.

    enum
    {
        TIMESTAMP = 1,
        CURRENT_DIVE_FILE = 2,
        DEPRECATED_BEHAVIOR_NUMBER = 3,
        PAYLOAD_BEHAVIOR_IDENTIFIER = 4,
        BEHAVIOR_TYPE = 5,
    };

    std::string behavior_type = nmea.at(BEHAVIOR_TYPE);
    glog.is(DEBUG1) && glog << "Bluefin began frontseat mission: " << behavior_type << std::endl;
}

void BluefinFrontSeat::bfboy(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        STATUS = 2,
        ERROR_CODE = 3,
        DEBUG_STRING = 4,
        BUOYANCY_ESTIMATE_NEWTONS = 5
    };

    gpb::FrontSeatInterfaceData data;
    gpb::BuoyancyStatus* buoy_status =
        data.MutableExtension(gpb::bluefin_data)->mutable_buoyancy_status();

    int status = nmea.as<int>(STATUS);
    if (gpb::BuoyancyStatus::Status_IsValid(status))
        buoy_status->set_status(static_cast<gpb::BuoyancyStatus::Status>(status));

    int error = nmea.as<int>(ERROR_CODE);
    if (gpb::BuoyancyStatus::Error_IsValid(error))
        buoy_status->set_error(static_cast<gpb::BuoyancyStatus::Error>(error));
    buoy_status->set_debug_string(nmea.at(DEBUG_STRING));
    buoy_status->set_buoyancy_newtons(nmea.as<double>(BUOYANCY_ESTIMATE_NEWTONS));
    signal_data_from_frontseat(data);
}

void BluefinFrontSeat::bftrm(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        STATUS = 2,
        ERROR_CODE = 3,
        DEBUG_STRING = 4,
        PITCH_DEGREES = 5,
        ROLL_DEGREES = 6
    };

    gpb::FrontSeatInterfaceData data;
    gpb::TrimStatus* trim_status = data.MutableExtension(gpb::bluefin_data)->mutable_trim_status();

    int status = nmea.as<int>(STATUS);
    int error = nmea.as<int>(ERROR_CODE);

    if (gpb::TrimStatus::Status_IsValid(status))
        trim_status->set_status(static_cast<gpb::TrimStatus::Status>(status));
    if (gpb::TrimStatus::Error_IsValid(error))
        trim_status->set_error(static_cast<gpb::TrimStatus::Error>(error));
    trim_status->set_debug_string(nmea.at(DEBUG_STRING));
    trim_status->set_pitch_trim_degrees(nmea.as<double>(PITCH_DEGREES));
    trim_status->set_roll_trim_degrees(nmea.as<double>(ROLL_DEGREES));

    signal_data_from_frontseat(data);
}

void BluefinFrontSeat::bfmbe(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        CURRENT_DIVE_FILE = 2,
        DEPRECATED_BEHAVIOR_NUMBER = 3,
        PAYLOAD_BEHAVIOR_IDENTIFIER = 4,
        BEHAVIOR_TYPE = 5,
    };

    std::string behavior_type = nmea.at(BEHAVIOR_TYPE);

    glog.is(DEBUG1) && glog << "Bluefin ended frontseat mission: " << behavior_type << std::endl;
}

void BluefinFrontSeat::bftop(const goby::util::NMEASentence& nmea)
{
    // Topside Message (Not Implemented)
    // Delivery of a message sent from the topside.
}

void BluefinFrontSeat::bfdvl(const goby::util::NMEASentence& nmea)
{
    // Raw DVL Data
}

void BluefinFrontSeat::bfmis(const goby::util::NMEASentence& nmea)
{
    std::string running = nmea.at(3);
    if (running.find("Running") != std::string::npos)
    {
        switch (bf_config_.accepting_commands_hook())
        {
            case BluefinFrontSeatConfig::BFMIS_RUNNING_TRIGGER:
                frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
                break;

            case BluefinFrontSeatConfig::BFCTL_TRIGGER:
            case BluefinFrontSeatConfig::BFMSC_TRIGGER:
                if (frontseat_state_ != gpb::FRONTSEAT_ACCEPTING_COMMANDS)
                    frontseat_state_ = gpb::FRONTSEAT_IN_CONTROL;
                break;

            default: break;
        }
    }
    else
    {
        frontseat_state_ = gpb::FRONTSEAT_IDLE;
    }
}

void BluefinFrontSeat::bfctd(const goby::util::NMEASentence& nmea)
{
    gpb::FrontSeatInterfaceData data;
    goby::moos::protobuf::CTDSample* ctd_sample = data.mutable_ctd_sample();

    enum
    {
        TIMESTAMP_SENT = 1,
        CONDUCTIVITY = 2,
        TEMPERATURE = 3,
        PRESSURE = 4,
        TIMESTAMP_DATA = 5
    };

    // Conductivity (uSiemens/cm -> Siemens/meter)
    ctd_sample->set_conductivity(nmea.as<double>(CONDUCTIVITY) / 1e4);

    // Temperature (degrees Celsius)
    ctd_sample->set_temperature(nmea.as<double>(TEMPERATURE));

    // Pressure (kPa -> Pascals)
    ctd_sample->set_pressure(nmea.as<double>(PRESSURE) * 1e3);
    compute_missing(ctd_sample);
    signal_data_from_frontseat(data);
}

void BluefinFrontSeat::bfctl(const goby::util::NMEASentence& nmea)
{
    if (bf_config_.accepting_commands_hook() == BluefinFrontSeatConfig::BFCTL_TRIGGER)
    {
        enum
        {
            TIMESTAMP = 1,
            CONTROL = 2
        };

        bool control = nmea.as<bool>(CONTROL);
        if (control)
            frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
        else if (frontseat_state_ == gpb::FRONTSEAT_ACCEPTING_COMMANDS)
            frontseat_state_ = gpb::FRONTSEAT_IN_CONTROL;
    }
}
