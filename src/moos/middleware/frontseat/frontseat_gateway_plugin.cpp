#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/frontseat/groups.h"

#include "goby/moos/middleware/moos_plugin_translator.h"
#include "goby/moos/moos_translator.h"
#include "goby/moos/protobuf/desired_course.pb.h"
#include "goby/moos/protobuf/node_status.pb.h"

using goby::glog;

namespace goby
{
namespace moos
{
class FrontSeatTranslation : public goby::moos::Translator
{
  public:
    FrontSeatTranslation(const goby::apps::moos::protobuf::GobyMOOSGatewayConfig& cfg)
        : goby::moos::Translator(cfg)
    {
        goby()
            .interprocess()
            .subscribe<goby::middleware::frontseat::groups::node_status,
                       goby::moos::protobuf::NodeStatus,
                       goby::middleware::MarshallingScheme::PROTOBUF>(
                [this](const goby::moos::protobuf::NodeStatus& status) {
                    convert_node_status(status);
                });

        std::vector<std::string> desired_buffer_params(
            {"SPEED", "HEADING", "DEPTH", "PITCH", "ROLL", "Z_RATE", "ALTITUDE"});
        for (const auto& var : desired_buffer_params) moos().add_buffer("DESIRED_" + var);
        moos().add_trigger("DESIRED_SPEED",
                           [this](const CMOOSMsg& msg) { convert_desired_setpoints(); });
    }

  private:
    void convert_node_status(const goby::moos::protobuf::NodeStatus& status);
    void convert_desired_setpoints();
};
} // namespace moos
} // namespace goby

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

void goby::moos::FrontSeatTranslation::convert_node_status(
    const goby::moos::protobuf::NodeStatus& status)
{
    // post NAV_*
    using boost::units::quantity;
    namespace si = boost::units::si;
    namespace degree = boost::units::degree;

    glog.is_debug2() && glog << "Posting to MOOS: NAV: " << status.DebugString() << std::endl;

    moos().comms().Notify("NAV_X", status.local_fix().x_with_units<quantity<si::length>>().value());
    moos().comms().Notify("NAV_Y", status.local_fix().y_with_units<quantity<si::length>>().value());
    moos().comms().Notify("NAV_LAT",
                          status.global_fix().lat_with_units() / boost::units::degree::degrees);
    moos().comms().Notify("NAV_LONG",
                          status.global_fix().lon_with_units() / boost::units::degree::degrees);

    if (status.local_fix().has_z())
        moos().comms().Notify("NAV_Z",
                              status.local_fix().z_with_units<quantity<si::length>>().value());
    if (status.global_fix().has_depth())
        moos().comms().Notify("NAV_DEPTH",
                              status.global_fix().depth_with_units<quantity<si::length>>().value());

    if (status.pose().has_heading())
        moos().comms().Notify(
            "NAV_HEADING",
            status.pose().heading_with_units<quantity<degree::plane_angle>>().value());

    moos().comms().Notify("NAV_SPEED", status.speed_with_units<quantity<si::velocity>>().value());

    if (status.pose().has_pitch())
        moos().comms().Notify("NAV_PITCH",
                              status.pose().pitch_with_units<quantity<si::plane_angle>>().value());
    if (status.pose().has_roll())
        moos().comms().Notify("NAV_ROLL",
                              status.pose().roll_with_units<quantity<si::plane_angle>>().value());

    if (status.global_fix().has_altitude())
        moos().comms().Notify(
            "NAV_ALTITUDE",
            status.global_fix().altitude_with_units<quantity<si::length>>().value());

    // surface for GPS variable
    if (status.global_fix().lat_source() == goby::moos::protobuf::GPS &&
        status.global_fix().lon_source() == goby::moos::protobuf::GPS)
    {
        std::stringstream ss;
        ss << "Timestamp=" << std::setprecision(15)
           << status.time_with_units() / boost::units::si::seconds;
        moos().comms().Notify("GPS_UPDATE_RECEIVED", ss.str());
    }
}

void goby::moos::FrontSeatTranslation::convert_desired_setpoints()
{
    goby::moos::protobuf::DesiredCourse desired_setpoints;

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
