// Copyright 2013-2020:
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

#include <algorithm> // for copy
#include <deque>     // for deque
#include <limits>    // for num...
#include <list>      // for ope...
#include <map>       // for map
#include <math.h>    // for sqrt
#include <memory>    // for all...
#include <ostream>   // for ios...
#include <string>    // for string

#include <boost/algorithm/string/case_conv.hpp>        // for to_...
#include <boost/algorithm/string/predicate.hpp>        // for ieq...
#include <boost/bimap.hpp>                             // for bimap
#include <boost/date_time/gregorian/gregorian.hpp>     // for gre...
#include <boost/function.hpp>                          // for fun...
#include <boost/lexical_cast/bad_lexical_cast.hpp>     // for bad...
#include <boost/signals2/expired_slot.hpp>             // for exp...
#include <boost/signals2/signal.hpp>                   // for signal
#include <boost/units/absolute.hpp>                    // for ope...
#include <boost/units/quantity.hpp>                    // for ope...
#include <boost/units/systems/si/length.hpp>           // for length
#include <boost/units/systems/si/time.hpp>             // for sec...
#include <boost/units/systems/si/velocity.hpp>         // for met...
#include <boost/units/systems/temperature/celsius.hpp> // for tem...

#include "goby/middleware/frontseat/bluefin/bluefin.pb.h"        // for Blu...
#include "goby/middleware/frontseat/bluefin/bluefin_config.pb.h" // for Blu...
#include "goby/middleware/protobuf/frontseat.pb.h"               // for Int...
#include "goby/middleware/protobuf/frontseat_data.pb.h"          // for Nod...
#include "goby/time/convert.h"                                   // for con...
#include "goby/time/simulation.h"                                // for time
#include "goby/time/system_clock.h"                              // for Sys...
#include "goby/time/types.h"                                     // for Mic...
#include "goby/util/as.h"                                        // for as
#include "goby/util/debug_logger/flex_ostream.h"                 // for Fle...
#include "goby/util/debug_logger/flex_ostreambuf.h"              // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"          // for warn
#include "goby/util/debug_logger/term_color.h"                   // for tcolor
#include "goby/util/linebasedcomms/nmea_sentence.h"              // for NME...
#include "goby/util/units/rpm/system.hpp"                        // for ang...

#include "bluefin.h" // for Blu...

namespace gpb = goby::middleware::frontseat::protobuf;
namespace gtime = goby::time;

using goby::glog;
using goby::util::NMEASentence;
using namespace goby::util::logger;
using namespace goby::util::tcolor;
using goby::middleware::frontseat::protobuf::BluefinConfig;

void goby::middleware::frontseat::Bluefin::bfack(const goby::util::NMEASentence& nmea)
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

    auto status = static_cast<AckStatus>(nmea.as<int>(ACK_STATUS));

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
                pending_.emplace_back(out_.front().message());
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
        if (sentence_id_map_.left.count(acked_sentence))
        {
            switch (sentence_id_map_.left.at(acked_sentence))
            {
                case RMB: type = gpb::BluefinExtraCommands::DESIRED_COURSE; break;
                case BOY: type = gpb::BluefinExtraCommands::BUOYANCY_ADJUST; break;
                case TRM: type = gpb::BluefinExtraCommands::TRIM_ADJUST; break;
                case SIL: type = gpb::BluefinExtraCommands::SILENT_MODE; break;
                case RCB: type = gpb::BluefinExtraCommands::CANCEL_CURRENT_BEHAVIOR; break;
                default: break;
            }
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

void goby::middleware::frontseat::Bluefin::bfmsc(const goby::util::NMEASentence& /*nmea*/)
{
    // TODO: See if there is something to the message contents
    // BF manual says: Arbitrary textual message. Semantics determined by the payload.
    if (bf_config_.accepting_commands_hook() == BluefinConfig::BFMSC_TRIGGER)
        frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
}

void goby::middleware::frontseat::Bluefin::bfnvg(const goby::util::NMEASentence& nmea)
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
    status_.set_time_with_units(
        gtime::convert_from_nmea<gtime::MicroTime>(nmea.at(COMPUTED_TIMESTAMP)));

    const std::string& lat_string = nmea.at(LATITUDE);
    if (lat_string.length() > 2)
    {
        auto lat_deg = goby::util::as<double>(lat_string.substr(0, 2));
        auto lat_min = goby::util::as<double>(lat_string.substr(2, lat_string.size()));
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
        auto lon_deg = goby::util::as<double>(lon_string.substr(0, 3));
        auto lon_min = goby::util::as<double>(lon_string.substr(3, nmea.at(4).size()));
        double lon = lon_deg + lon_min / 60;
        status_.mutable_global_fix()->set_lon((nmea.at(LON_HEMISPHERE) == "W") ? -lon : lon);
    }
    else
    {
        status_.mutable_global_fix()->set_lon(std::numeric_limits<double>::quiet_NaN());
    }

    if (nmea.as<int>(QUALITY_OF_POSITION) == 1)
    {
        status_.mutable_source()->set_position(gpb::Source::GPS);
    }

    status_.mutable_global_fix()->set_altitude(nmea.as<double>(ALTITUDE));
    status_.mutable_global_fix()->set_depth(nmea.as<double>(DEPTH));
    status_.mutable_pose()->set_heading(nmea.as<double>(HEADING));
    status_.mutable_pose()->set_roll(nmea.as<double>(ROLL));
    status_.mutable_pose()->set_pitch(nmea.as<double>(PITCH));
}

void goby::middleware::frontseat::Bluefin::bfnvr(const goby::util::NMEASentence& nmea)
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

    // auto status_time = status_.time_with_units();
    // auto dt = gtime::convert_from_nmea<decltype(status_time)>(nmea.at(TIMESTAMP)) - status_time;

    auto east_speed = nmea.as<double>(EAST_VELOCITY);
    auto north_speed = nmea.as<double>(NORTH_VELOCITY);

    status_.mutable_pose()->set_pitch_rate(nmea.as<double>(PITCH_RATE));
    status_.mutable_pose()->set_roll_rate(nmea.as<double>(ROLL_RATE));
    status_.mutable_pose()->set_heading_rate(nmea.as<double>(YAW_RATE));
    status_.mutable_speed()->set_over_ground(
        std::sqrt(north_speed * north_speed + east_speed * east_speed));

    //    status_.mutable_pose()->set_roll_rate_time_lag_with_units(dt);
    //    status_.mutable_pose()->set_pitch_rate_time_lag_with_units(dt);
    //    status_.mutable_pose()->set_heading_rate_time_lag_with_units(dt);
    //    status_.set_speed_time_lag_with_units(dt);

    // fill in the local X, Y
    compute_missing(&status_);

    gpb::InterfaceData data;
    data.mutable_node_status()->CopyFrom(status_);
    signal_data_from_frontseat(data);
}

void goby::middleware::frontseat::Bluefin::bfsvs(const goby::util::NMEASentence& nmea)
{
    // If the Bluefin vehicle is equipped with a sound velocity sensor, this message will provide the raw output of that sensor. If not, then an estimated value will be provided.

    // We don't use this, choosing to calculate it ourselves from the CTD
}

void goby::middleware::frontseat::Bluefin::bfsht(const goby::util::NMEASentence& /*nmea*/)
{
    glog.is(WARN) && glog << "Bluefin sent us the SHT message: they are shutting down!"
                          << std::endl;
}

void goby::middleware::frontseat::Bluefin::bfmbs(const goby::util::NMEASentence& nmea)
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

void goby::middleware::frontseat::Bluefin::bfboy(const goby::util::NMEASentence& nmea)
{
    enum
    {
        TIMESTAMP = 1,
        STATUS = 2,
        ERROR_CODE = 3,
        DEBUG_STRING = 4,
        BUOYANCY_ESTIMATE_NEWTONS = 5
    };

    gpb::InterfaceData data;
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

void goby::middleware::frontseat::Bluefin::bftrm(const goby::util::NMEASentence& nmea)
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

    gpb::InterfaceData data;
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

void goby::middleware::frontseat::Bluefin::bfmbe(const goby::util::NMEASentence& nmea)
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

void goby::middleware::frontseat::Bluefin::bftop(const goby::util::NMEASentence& nmea)
{
    // Topside Message (Not Implemented)
    // Delivery of a message sent from the topside.
}

void goby::middleware::frontseat::Bluefin::bfdvl(const goby::util::NMEASentence& nmea)
{
    // $BFDVL,hhmmss.ss,x.x,y.y,z.z,r1,r2,r3,r4,t.t,hhmmss.ss*hh
    //hhmmss.ss Timestamp (when message is sent)
    //x.x X velocity (m/s) positive is forward
    //y.y Y velocity (m/s) positive is to starboard
    //z.z Z velocity (m/s) positive is down
    //r1-r4 Beam ranges (m/s) - ???
    //t.t Temperature (C)
    //hhmmss.ss Timestamp (when DVL data was received)
    enum
    {
        TIMESTAMP = 1,
        X_VEL = 2,
        Y_VEL = 3,
        Z_VEL = 4,
        R1 = 5,
        R2 = 6,
        R3 = 7,
        R4 = 8,
        TEMPERATURE = 9,
        DVL_TIMESTAMP = 10
    };

    gpb::InterfaceData data;

    auto& dvl_data = *data.MutableExtension(gpb::bluefin_data)->mutable_dvl();

    using boost::units::si::meters_per_second;
    if (!nmea.at(X_VEL).empty())
        dvl_data.set_u_with_units(nmea.as<double>(X_VEL) * meters_per_second);
    if (!nmea.at(Y_VEL).empty())
        dvl_data.set_v_with_units(nmea.as<double>(Y_VEL) * meters_per_second);
    if (!nmea.at(Z_VEL).empty())
        dvl_data.set_w_with_units(nmea.as<double>(Z_VEL) * meters_per_second);

    // TODO: check units on ranges
    using boost::units::si::meters;
    if (!nmea.at(R1).empty())
        dvl_data.set_beam1_range_with_units(nmea.as<double>(R1) * meters);
    if (!nmea.at(R2).empty())
        dvl_data.set_beam2_range_with_units(nmea.as<double>(R2) * meters);
    if (!nmea.at(R3).empty())
        dvl_data.set_beam3_range_with_units(nmea.as<double>(R3) * meters);
    if (!nmea.at(R4).empty())
        dvl_data.set_beam4_range_with_units(nmea.as<double>(R4) * meters);

    using boost::units::absolute;
    if (!nmea.at(TEMPERATURE).empty())
        dvl_data.set_temperature_with_units(nmea.as<double>(TEMPERATURE) *
                                            absolute<boost::units::celsius::temperature>());

    if (!nmea.at(DVL_TIMESTAMP).empty())
        dvl_data.set_dvl_time_with_units(nmea.as<double>(DVL_TIMESTAMP) *
                                         boost::units::si::seconds);

    signal_data_from_frontseat(data);
}

void goby::middleware::frontseat::Bluefin::bfrvl(const goby::util::NMEASentence& nmea)
{
    // Vehicle velocity through water as estimated from thruster RPM, may be empty if no lookup table is implemented (m/s)
    enum
    {
        TIMESTAMP = 1,
        THRUSTER_RPM = 2,
        SPEED_FROM_LOOKUP_TABLE = 3
    };

    gpb::InterfaceData data;

    auto& thruster_data = *data.MutableExtension(gpb::bluefin_data)->mutable_thruster();
    thruster_data.set_rotation_with_units(nmea.as<double>(THRUSTER_RPM) *
                                          goby::util::units::rpm::rpms_omega);

    if (!nmea.at(SPEED_FROM_LOOKUP_TABLE).empty())
        thruster_data.set_speed_from_lookup_table_with_units(
            nmea.as<double>(SPEED_FROM_LOOKUP_TABLE) * boost::units::si::meters_per_second);

    signal_data_from_frontseat(data);
}

void goby::middleware::frontseat::Bluefin::bfmis(const goby::util::NMEASentence& nmea)
{
    std::string running = nmea.at(3);
    if (running.find("Running") != std::string::npos)
    {
        switch (bf_config_.accepting_commands_hook())
        {
            case BluefinConfig::BFMIS_RUNNING_TRIGGER:
                frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
                break;

            case BluefinConfig::BFCTL_TRIGGER:
            case BluefinConfig::BFMSC_TRIGGER:
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

void goby::middleware::frontseat::Bluefin::bfctd(const goby::util::NMEASentence& nmea)
{
    gpb::InterfaceData data;
    gpb::CTDSample* ctd_sample = data.mutable_ctd_sample();

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

void goby::middleware::frontseat::Bluefin::bfctl(const goby::util::NMEASentence& nmea)
{
    if (bf_config_.accepting_commands_hook() == BluefinConfig::BFCTL_TRIGGER)
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
