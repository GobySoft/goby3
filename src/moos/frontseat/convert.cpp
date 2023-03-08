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

#include "convert.h"

#include <iomanip> // for operator<<
#include <ostream> // for basic_ostream

#include <MOOS/libMOOS/Comms/MOOSCommClient.h>    // for CMOOSCommClient
#include <boost/units/base_units/si/meter.hpp>    // for si
#include <boost/units/quantity.hpp>               // for quantity
#include <boost/units/systems/angle/degrees.hpp>  // for degrees, pla...
#include <boost/units/systems/si/length.hpp>      // for length
#include <boost/units/systems/si/plane_angle.hpp> // for plane_angle
#include <boost/units/systems/si/time.hpp>        // for seconds
#include <boost/units/systems/si/velocity.hpp>    // for velocity

#include "goby/middleware/protobuf/frontseat_data.pb.h" // for NodeStatus

void goby::moos::convert_and_publish_node_status(
    const goby::middleware::frontseat::protobuf::NodeStatus& status, CMOOSCommClient& moos_comms)
{
    // post NAV_*
    using boost::units::quantity;
    namespace si = boost::units::si;
    namespace degree = boost::units::degree;

    moos_comms.Notify("NAV_X", status.local_fix().x_with_units<quantity<si::length>>().value());
    moos_comms.Notify("NAV_Y", status.local_fix().y_with_units<quantity<si::length>>().value());
    moos_comms.Notify("NAV_LAT",
                      status.global_fix().lat_with_units() / boost::units::degree::degrees);
    moos_comms.Notify("NAV_LONG",
                      status.global_fix().lon_with_units() / boost::units::degree::degrees);

    if (status.local_fix().has_z())
        moos_comms.Notify("NAV_Z", status.local_fix().z_with_units<quantity<si::length>>().value());
    if (status.global_fix().has_depth())
        moos_comms.Notify("NAV_DEPTH",
                          status.global_fix().depth_with_units<quantity<si::length>>().value());

    if (status.pose().has_heading()) {
        moos_comms.Notify(
            "NAV_HEADING",
            status.pose().heading_with_units<quantity<degree::plane_angle>>().value());
        moos_comms.Notify(
            "NAV_YAW",
            -status.pose().heading_with_units<quantity<si::plane_angle>>().value());
    }

    moos_comms.Notify("NAV_SPEED",
                      status.speed().over_ground_with_units<quantity<si::velocity>>().value());

    if (status.pose().has_pitch())
        moos_comms.Notify("NAV_PITCH",
                          status.pose().pitch_with_units<quantity<si::plane_angle>>().value());
    if (status.pose().has_roll())
        moos_comms.Notify("NAV_ROLL",
                          status.pose().roll_with_units<quantity<si::plane_angle>>().value());

    if (status.global_fix().has_altitude())
        moos_comms.Notify("NAV_ALTITUDE",
                          status.global_fix().altitude_with_units<quantity<si::length>>().value());

    // surface for GPS variable
    if (status.source().position() == goby::middleware::frontseat::protobuf::Source::GPS)
    {
        std::stringstream ss;
        ss << "Timestamp=" << std::setprecision(15)
           << status.time_with_units() / boost::units::si::seconds;
        moos_comms.Notify("GPS_UPDATE_RECEIVED", ss.str());
    }
}
