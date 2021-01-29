// Copyright 2020-2021:
//   GobySoft, LLC (2013-)
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

#include <chrono>      // for seconds
#include <list>        // for operator!=
#include <ostream>     // for operator<<
#include <ratio>       // for ratio
#include <type_traits> // for __decay_an...
#include <unistd.h>    // for sleep
#include <utility>     // for pair, make...
#include <vector>      // for vector

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF
#include <boost/algorithm/string/split.hpp>          // for split
#include <boost/algorithm/string/trim.hpp>           // for trim
#include <boost/detail/basic_pointerbuf.hpp>         // for basic_poin...
#include <boost/function.hpp>                        // for function
#include <boost/lexical_cast/bad_lexical_cast.hpp>   // for bad_lexica...
#include <boost/signals2/expired_slot.hpp>           // for expired_slot
#include <boost/signals2/signal.hpp>                 // for signal

#include "goby/middleware/protobuf/frontseat_config.pb.h" // for Config
#include "goby/middleware/protobuf/frontseat_data.pb.h"   // for NodeStatus
#include "goby/util/as.h"                                 // for as
#include "goby/util/debug_logger/flex_ostream.h"          // for operator<<
#include "goby/util/debug_logger/flex_ostreambuf.h"       // for VERBOSE
#include "goby/util/debug_logger/logger_manipulators.h"   // for warn
#include "goby/util/debug_logger/term_color.h"            // for tcolor

#include "basic_simulator_frontseat_driver.h"

using goby::glog;
using namespace goby::util::logger;
using namespace goby::util::tcolor;

const goby::time::SystemClock::duration allowed_skew{std::chrono::seconds(10)};

// allows iFrontSeat to load our library
extern "C"
{
    goby::middleware::frontseat::InterfaceBase*
    frontseat_driver_load(goby::middleware::frontseat::protobuf::Config* cfg)
    {
        return new goby::middleware::frontseat::BasicSimulatorFrontSeatInterface(*cfg);
    }
}

goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::BasicSimulatorFrontSeatInterface(
    const goby::middleware::frontseat::protobuf::Config& cfg)
    : InterfaceBase(cfg),
      sim_config_(cfg.GetExtension(protobuf::basic_simulator_config)),
      tcp_(sim_config_.tcp_address(), sim_config_.tcp_port()),
      frontseat_providing_data_(false),
      last_frontseat_data_time_(std::chrono::seconds(0)),
      frontseat_state_(goby::middleware::frontseat::protobuf::FRONTSEAT_NOT_CONNECTED)
{
    tcp_.start();

    // wait for up to 10 seconds for a connection
    // in a real driver, should keep trying to reconnect (maybe with a backoff).
    int timeout = 10, i = 0;
    while (!tcp_.active() && i < timeout)
    {
        ++i;
        sleep(1);
    }
}

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::loop()
{
    check_connection_state();
    try_receive();

    // if we haven't gotten data for a while, set this boolean so that the
    // InterfaceBase class knows
    if (goby::time::SystemClock::now() > last_frontseat_data_time_ + allowed_skew)
        frontseat_providing_data_ = false;
} // loop

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::check_connection_state()
{
    // check the connection state
    if (!tcp_.active())
    {
        // in a real driver, change this to try to reconnect (see Bluefin driver example)
        glog.is(DIE) && glog << "Connection to FrontSeat failed: " << sim_config_.tcp_address()
                             << ":" << sim_config_.tcp_port() << std::endl;
    }
    else
    {
        // on connection, send the START command to initialize the simulator
        if (frontseat_state_ == goby::middleware::frontseat::protobuf::FRONTSEAT_NOT_CONNECTED)
        {
            glog.is(VERBOSE) && glog << "Connected to Basic Vehicle Simulator." << std::endl;
            frontseat_state_ = goby::middleware::frontseat::protobuf::FRONTSEAT_IDLE;
            std::stringstream start_ss;
            start_ss << "START,"
                     << "LAT:" << sim_config_.start().lat() << ","
                     << "LON:" << sim_config_.start().lon() << ","
                     << "DURATION:" << sim_config_.start().duration() << ","
                     << "FREQ:" << sim_config_.start().control_freq() << ","
                     << "ACCEL:" << sim_config_.start().vehicle().accel() << ","
                     << "HDG_RATE:" << sim_config_.start().vehicle().hdg_rate() << ","
                     << "Z_RATE:" << sim_config_.start().vehicle().z_rate() << ","
                     << "WARP:" << cfg().sim_warp_factor();
            write(start_ss.str());
        }
    }
}

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::try_receive()
{
    std::string in;
    while (tcp_.readline(&in))
    {
        boost::trim(in);
        try
        {
            process_receive(in);
        }
        catch (std::exception& e)
        {
            glog.is(DEBUG1) && glog << warn << "Failed to handle message: " << e.what()
                                    << std::endl;
        }
    }
}

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::process_receive(
    const std::string& s)
{
    goby::middleware::frontseat::protobuf::Raw raw_msg;
    raw_msg.set_raw(s);
    signal_raw_from_frontseat(raw_msg);

    std::map<std::string, std::string> parsed;
    parse_in(s, &parsed);

    // frontseat state message
    if (parsed["KEY"] == "CTRL")
    {
        if (parsed["STATE"] == "PAYLOAD")
            frontseat_state_ = goby::middleware::frontseat::protobuf::FRONTSEAT_ACCEPTING_COMMANDS;
        else if (parsed["STATE"] == "AUV")
            frontseat_state_ = goby::middleware::frontseat::protobuf::FRONTSEAT_IN_CONTROL;
        else
            frontseat_state_ = goby::middleware::frontseat::protobuf::FRONTSEAT_IDLE;
    }
    // frontseat navigation message
    else if (parsed["KEY"] == "NAV")
    {
        goby::middleware::frontseat::protobuf::InterfaceData data;
        goby::middleware::frontseat::protobuf::NodeStatus& status = *data.mutable_node_status();

        glog.is(VERBOSE) && glog << "Got NAV update: " << s << std::endl;
        status.mutable_pose()->set_heading(goby::util::as<double>(parsed["HEADING"]));
        status.mutable_speed()->set_over_ground(goby::util::as<double>(parsed["SPEED"]));
        status.mutable_global_fix()->set_depth(goby::util::as<double>(parsed["DEPTH"]));
        status.mutable_global_fix()->set_lon(goby::util::as<double>(parsed["LON"]));
        status.mutable_global_fix()->set_lat(goby::util::as<double>(parsed["LAT"]));

        // calculates the local fix (X, Y, Z) from global fix
        compute_missing(&status);

        signal_data_from_frontseat(data);

        frontseat_providing_data_ = true;
        last_frontseat_data_time_ = goby::time::SystemClock::now();
    }
    // frontseat response to our command message
    else if (parsed["KEY"] == "CMD")
    {
        if (last_request_.response_requested())
        {
            goby::middleware::frontseat::protobuf::CommandResponse response;
            response.set_request_successful(parsed["RESULT"] == "OK");
            response.set_request_id(last_request_.request_id());
            signal_command_response(response);
        }
    }
    else
    {
        glog.is(VERBOSE) && glog << "Unknown message from frontseat: " << s << std::endl;
    }
}

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::send_command_to_frontseat(
    const goby::middleware::frontseat::protobuf::CommandRequest& command)
{
    if (command.has_desired_course())
    {
        std::stringstream cmd_ss;
        const goby::middleware::frontseat::protobuf::DesiredCourse& desired_course =
            command.desired_course();
        cmd_ss << "CMD,"
               << "HEADING:" << desired_course.heading() << ","
               << "SPEED:" << desired_course.speed() << ","
               << "DEPTH:" << desired_course.depth();

        write(cmd_ss.str());
        last_request_ = command;
    }
    else
    {
        glog.is(VERBOSE) && glog << "Unhandled command: " << command.ShortDebugString()
                                 << std::endl;
    }

} // send_command_to_frontseat

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::send_data_to_frontseat(
    const goby::middleware::frontseat::protobuf::InterfaceData& data)
{
    // Bsaic simulator driver doesn't have any data to sent to the frontseat
} // send_data_to_frontseat

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::send_raw_to_frontseat(
    const goby::middleware::frontseat::protobuf::Raw& data)
{
    write(data.raw());
} // send_raw_to_frontseat

bool goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::frontseat_providing_data() const
{
    return frontseat_providing_data_;
} // frontseat_providing_data

goby::middleware::frontseat::protobuf::FrontSeatState
goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::frontseat_state() const
{
    return frontseat_state_;
} // frontseat_state

void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::write(const std::string& s)
{
    goby::middleware::frontseat::protobuf::Raw raw_msg;
    raw_msg.set_raw(s);
    signal_raw_to_frontseat(raw_msg);

    tcp_.write(s + "\r\n");
}

// transforms a string of format "{field0},{key1}:{field1},{key2}:{field2} into a map of
// "KEY"=>{field0}
// {key1}=>{field1}
// {key2}=>{field2}
void goby::middleware::frontseat::BasicSimulatorFrontSeatInterface::parse_in(
    const std::string& in, std::map<std::string, std::string>* out)
{
    std::vector<std::string> comma_split;
    boost::split(comma_split, in, boost::is_any_of(","));
    out->insert(std::make_pair("KEY", comma_split.at(0)));
    for (int i = 1, n = comma_split.size(); i < n; ++i)
    {
        std::vector<std::string> colon_split;
        boost::split(colon_split, comma_split[i], boost::is_any_of(":"));
        out->insert(std::make_pair(colon_split.at(0), colon_split.at(1)));
    }
}
