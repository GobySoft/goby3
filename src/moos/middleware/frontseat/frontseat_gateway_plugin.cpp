// Copyright 2019-2021:
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

#include <map>     // for map, operator!=
#include <utility> // for pair

#include <boost/units/quantity.hpp>              // for operator*, quantity
#include <boost/units/systems/angle/degrees.hpp> // for degrees, plane_angle
#include <boost/units/systems/si/length.hpp>     // for length, meters
#include <boost/units/systems/si/time.hpp>       // for seconds, time
#include <boost/units/systems/si/velocity.hpp>   // for meters_per_second

#include "goby/zeromq/application/multi_thread.h"

#include "frontseat_gateway_plugin.h"

namespace goby
{
namespace apps
{
namespace moos
{
namespace protobuf
{
class GobyMOOSGatewayConfig;
} // namespace protobuf
} // namespace moos
} // namespace apps
} // namespace goby

using goby::glog;

extern "C"
{
    void goby3_moos_gateway_load(
        goby::zeromq::MultiThreadApplication<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>*
            handler)
    {
        handler->launch_thread<goby::moos::FrontSeatTranslation>();
    }

    void goby3_moos_gateway_unload(
        goby::zeromq::MultiThreadApplication<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>*
            handler)
    {
        handler->join_thread<goby::moos::FrontSeatTranslation>();
    }
}

void goby::moos::FrontSeatTranslation::convert_desired_setpoints()
{
    goby::middleware::frontseat::protobuf::DesiredCourse desired_setpoints;

    auto& buffer = moos().buffer();

    auto speed_it = buffer.find("DESIRED_SPEED");
    desired_setpoints.set_time_with_units(speed_it->second.GetTime() * boost::units::si::seconds);
    desired_setpoints.set_speed_with_units(speed_it->second.GetDouble() *
                                           boost::units::si::meters_per_second);

    auto heading_it = buffer.find("DESIRED_HEADING");
    if (heading_it != buffer.end())
    {
        desired_setpoints.set_heading_with_units(heading_it->second.GetDouble() *
                                                 boost::units::degree::degrees);
        buffer.erase(heading_it);
    }
    auto pitch_it = buffer.find("DESIRED_PITCH");
    if (pitch_it != buffer.end())
    {
        desired_setpoints.set_pitch_with_units(pitch_it->second.GetDouble() *
                                               boost::units::degree::degrees);
        buffer.erase(pitch_it);
    }
    auto roll_it = buffer.find("DESIRED_ROLL");
    if (roll_it != buffer.end())
    {
        desired_setpoints.set_roll_with_units(roll_it->second.GetDouble() *
                                              boost::units::degree::degrees);
        buffer.erase(roll_it);
    }

    auto depth_it = buffer.find("DESIRED_DEPTH");
    if (depth_it != buffer.end())
    {
        desired_setpoints.set_depth_with_units(depth_it->second.GetDouble() *
                                               boost::units::si::meters);
        buffer.erase(depth_it);
    }
    auto altitude_it = buffer.find("DESIRED_ALTITUDE");
    if (altitude_it != buffer.end())
    {
        desired_setpoints.set_altitude_with_units(altitude_it->second.GetDouble() *
                                                  boost::units::si::meters);
        buffer.erase(altitude_it);
    }

    auto z_rate_it = buffer.find("DESIRED_Z_RATE");
    if (z_rate_it != buffer.end())
    {
        desired_setpoints.set_z_rate_with_units(z_rate_it->second.GetDouble() *
                                                boost::units::si::meters_per_second);
        buffer.erase(z_rate_it);
    }

    glog.is_debug2() && glog << "Posting to Goby: Desired: " << desired_setpoints.DebugString()
                             << std::endl;

    goby()
        .interprocess()
        .publish<goby::middleware::frontseat::groups::desired_course, decltype(desired_setpoints),
                 goby::middleware::MarshallingScheme::PROTOBUF>(desired_setpoints);
}
