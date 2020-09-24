#include "convert.h"

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

    if (status.pose().has_heading())
        moos_comms.Notify(
            "NAV_HEADING",
            status.pose().heading_with_units<quantity<degree::plane_angle>>().value());

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
