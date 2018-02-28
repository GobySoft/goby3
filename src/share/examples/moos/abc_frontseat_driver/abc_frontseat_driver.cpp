// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/common/logger.h"
#include "goby/util/as.h"

#include "abc_frontseat_driver.h"

namespace gpb = goby::moos::protobuf;
using goby::common::goby_time;
using goby::glog;
using namespace goby::common::logger;
using namespace goby::common::tcolor;

const int allowed_skew = 10;

// allows iFrontSeat to load our library
extern "C"
{
    FrontSeatInterfaceBase* frontseat_driver_load(iFrontSeatConfig* cfg)
    {
        return new AbcFrontSeat(*cfg);
    }
}

AbcFrontSeat::AbcFrontSeat(const iFrontSeatConfig& cfg)
    : FrontSeatInterfaceBase(cfg),
      abc_config_(cfg.GetExtension(abc_config)),
      tcp_(abc_config_.tcp_address(),
           abc_config_.tcp_port()),
      frontseat_providing_data_(false),
      last_frontseat_data_time_(0),
      frontseat_state_(gpb::FRONTSEAT_NOT_CONNECTED)
{
    tcp_.start();

    // wait for up to 10 seconds for a connection
    // in a real driver, should keep trying to reconnect (maybe with a backoff).
    int timeout = 10, i = 0;
    while(!tcp_.active() && i < timeout)
    {
        ++i;
        sleep(1);
    }
}

void AbcFrontSeat::loop()
{
    check_connection_state();
    try_receive();
    
    // if we haven't gotten data for a while, set this boolean so that the
    // FrontSeatInterfaceBase class knows
    if(goby_time<double>() > last_frontseat_data_time_ + allowed_skew)
        frontseat_providing_data_ = false;
} // loop

void AbcFrontSeat::check_connection_state()
{
    // check the connection state
    if(!tcp_.active())
    {
        // in a real driver, change this to try to reconnect (see Bluefin driver example)
        glog.is(DIE) && glog << "Connection to FrontSeat failed: " << abc_config_.tcp_address() << ":" << abc_config_.tcp_port() << std::endl;
    }
    else
    {
        // on connection, send the START command to initialize the simulator
        if(frontseat_state_ == gpb::FRONTSEAT_NOT_CONNECTED)
        {
            glog.is(VERBOSE) && glog << "Connected to ABC Simulator." << std::endl;
            frontseat_state_ = gpb::FRONTSEAT_IDLE;
            std::stringstream start_ss;
            start_ss << "START,"
                     << "LAT:" << abc_config_.start().lat() << ","
                     << "LON:" << abc_config_.start().lon() << ","
                     << "DURATION:" << abc_config_.start().duration();
            write(start_ss.str());
        }
    }
}


void AbcFrontSeat::try_receive()
{
    std::string in;
    while(tcp_.readline(&in))
    {
        boost::trim(in);
        try
        {
            process_receive(in);
        }
        catch(std::exception& e)
        {
            glog.is(DEBUG1) && glog << warn << "Failed to handle message: " << e.what() << std::endl;
        }            
    }
}    


void AbcFrontSeat::process_receive(const std::string& s)
{
    gpb::FrontSeatRaw raw_msg;
    raw_msg.set_raw(s);
    signal_raw_from_frontseat(raw_msg);

    std::map<std::string, std::string> parsed;
    parse_in(s, &parsed);

    // frontseat state message
    if(parsed["KEY"] == "CTRL")
    {
        if(parsed["STATE"] == "PAYLOAD")
            frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
        else if(parsed["STATE"] == "AUV")
            frontseat_state_ = gpb::FRONTSEAT_IN_CONTROL;
        else 
            frontseat_state_ = gpb::FRONTSEAT_IDLE;
    }
    // frontseat navigation message
    else if(parsed["KEY"] == "NAV")
    {
        gpb::FrontSeatInterfaceData data;
        goby::moos::protobuf::NodeStatus& status = *data.mutable_node_status();
        
        glog.is(VERBOSE) && glog << "Got NAV update: " << s << std::endl;
        status.mutable_pose()->set_heading(goby::util::as<double>(parsed["HEADING"]));
        status.set_speed(goby::util::as<double>(parsed["SPEED"]));
        status.mutable_global_fix()->set_depth(goby::util::as<double>(parsed["DEPTH"]));
        status.mutable_global_fix()->set_lon(goby::util::as<double>(parsed["LON"]));
        status.mutable_global_fix()->set_lat(goby::util::as<double>(parsed["LAT"]));

        // calculates the local fix (X, Y, Z) from global fix 
        compute_missing(&status);
    
        signal_data_from_frontseat(data);

        frontseat_providing_data_ = true;
        last_frontseat_data_time_ = goby_time<double>();
    }
    // frontseat response to our command message
    else if(parsed["KEY"] == "CMD")
    {
        if(last_request_.response_requested())
        {
            gpb::CommandResponse response;
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

void AbcFrontSeat::send_command_to_frontseat(const gpb::CommandRequest& command)
{    
    if(command.has_desired_course())
    {
        std::stringstream cmd_ss;
        const goby::moos::protobuf::DesiredCourse& desired_course = command.desired_course();
        cmd_ss << "CMD," 
               << "HEADING:" << desired_course.heading() << ","
               << "SPEED:" << desired_course.speed() << ","
               << "DEPTH:" << desired_course.depth();
        
        write(cmd_ss.str());
        last_request_ = command;
    }
    else
    {
        glog.is(VERBOSE) && glog << "Unhandled command: " << command.ShortDebugString() << std::endl;
    }
    
} // send_command_to_frontseat
    
void AbcFrontSeat::send_data_to_frontseat(const gpb::FrontSeatInterfaceData& data)
{
    // ABC driver doesn't have any data to sent to the frontseat
} // send_data_to_frontseat

void AbcFrontSeat::send_raw_to_frontseat(const gpb::FrontSeatRaw& data)
{
    write(data.raw());
} // send_raw_to_frontseat

    
bool AbcFrontSeat::frontseat_providing_data() const
{
    return frontseat_providing_data_;
} // frontseat_providing_data

goby::moos::protobuf::FrontSeatState AbcFrontSeat::frontseat_state() const
{
    return frontseat_state_;
} // frontseat_state

void AbcFrontSeat::write(const std::string& s)
{
    gpb::FrontSeatRaw raw_msg;
    raw_msg.set_raw(s);
    signal_raw_to_frontseat(raw_msg);
    
    tcp_.write(s + "\r\n");
}

// transforms a string of format "{field0},{key1}:{field1},{key2}:{field2} into a map of
// "KEY"=>{field0}
// {key1}=>{field1}
// {key2}=>{field2}
void AbcFrontSeat::parse_in(const std::string& in, std::map<std::string, std::string>* out)
{
    std::vector<std::string> comma_split;
    boost::split(comma_split, in, boost::is_any_of(","));
    out->insert(std::make_pair("KEY", comma_split.at(0)));
    for(int i = 1, n = comma_split.size(); i < n; ++i)
    {
        std::vector<std::string> colon_split;
        boost::split(colon_split, comma_split[i],
                     boost::is_any_of(":"));
        out->insert(std::make_pair(colon_split.at(0),
                                   colon_split.at(1)));
    }
}


