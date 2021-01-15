// Copyright 2020:
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

#ifndef GOBY_MOOS_MIDDLEWARE_FRONTSEAT_FRONTSEAT_GATEWAY_PLUGIN_H
#define GOBY_MOOS_MIDDLEWARE_FRONTSEAT_FRONTSEAT_GATEWAY_PLUGIN_H

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
                           [this](const CMOOSMsg& /*msg*/) { convert_desired_setpoints(); });

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

#endif
