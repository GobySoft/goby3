// Copyright 2019:
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

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/coroner/coroner.h"

#include "goby/moos/middleware/moos_plugin_translator.h"
#include "goby/moos/moos_translator.h"

using goby::glog;

namespace goby
{
namespace moos
{
class CoronerTranslation : public goby::moos::Translator
{
  public:
    CoronerTranslation(const goby::apps::moos::protobuf::GobyMOOSGatewayConfig& cfg)
        : goby::moos::Translator(cfg)
    {
        goby()
            .interprocess()
            .subscribe<goby::middleware::groups::health_report,
                       goby::middleware::protobuf::VehicleHealth>(
                [this](const goby::middleware::protobuf::VehicleHealth& health) {
                    glog.is_debug2() && glog << "To MOOS: " << health.ShortDebugString()
                                             << std::endl;

                    std::string serialized;
                    bool is_binary = serialize_for_moos(&serialized, health);
                    CMOOSMsg moos_msg = goby::moos::MOOSTranslator::make_moos_msg(
                        "GOBY_PROCESS_HEALTH", serialized, is_binary, goby::moos::moos_technique,
                        health.GetDescriptor()->full_name());
                    moos().comms().Post(moos_msg);
                });
    }

  private:
    goby::moos::MOOSTranslator translator_;
};
} // namespace moos
} // namespace goby

extern "C"
{
    void goby3_moos_gateway_load(
        goby::zeromq::MultiThreadApplication<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>*
            handler)
    {
        handler->launch_thread<goby::moos::CoronerTranslation>();
    }

    void goby3_moos_gateway_unload(
        goby::zeromq::MultiThreadApplication<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>*
            handler)
    {
        handler->join_thread<goby::moos::CoronerTranslation>();
    }
}
