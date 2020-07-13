#pragma once

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/frontseat/groups.h"
#include "goby/middleware/protobuf/frontseat.pb.h"
#include "goby/middleware/protobuf/frontseat_data.pb.h"
#include "goby/moos/frontseat/convert.h"
#include "goby/moos/middleware/moos_plugin_translator.h"
#include "goby/moos/moos_translator.h"

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
                       goby::middleware::frontseat::protobuf::NodeStatus,
                       goby::middleware::MarshallingScheme::PROTOBUF>(
                [this](const goby::middleware::frontseat::protobuf::NodeStatus& status) {
                    glog.is_debug2() && glog << "Posting to MOOS: NAV: " << status.DebugString()
                                             << std::endl;
                    goby::moos::convert_and_publish_node_status(status, moos().comms());
                });

        std::vector<std::string> desired_buffer_params(
            {"SPEED", "HEADING", "DEPTH", "PITCH", "ROLL", "Z_RATE", "ALTITUDE"});
        for (const auto& var : desired_buffer_params) moos().add_buffer("DESIRED_" + var);
        moos().add_trigger("DESIRED_SPEED",
                           [this](const CMOOSMsg& msg) { convert_desired_setpoints(); });

        moos().add_trigger("IVPHELM_STATE", [this](const CMOOSMsg& msg) {
            goby::middleware::frontseat::protobuf::HelmStateReport helm_state_report;
            std::string sval = msg.GetString();
            boost::trim(sval);
            if (boost::iequals(sval, "drive"))
                helm_state_report.set_state(goby::middleware::frontseat::protobuf::HELM_DRIVE);
            else if (boost::iequals(sval, "park"))
                helm_state_report.set_state(goby::middleware::frontseat::protobuf::HELM_PARK);
            else
                helm_state_report.set_state(
                    goby::middleware::frontseat::protobuf::HELM_NOT_RUNNING);
            goby().interprocess().publish<goby::middleware::frontseat::groups::helm_state>(
                helm_state_report);
        });
    }

  private:
    void convert_desired_setpoints();
};
} // namespace moos
} // namespace goby
