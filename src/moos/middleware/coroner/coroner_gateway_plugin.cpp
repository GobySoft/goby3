// Copyright 2019-2020:
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

#include <ostream> // for basic_ostream
#include <string>  // for operator<<
#include <vector>  // for vector

#include <MOOS/libMOOS/Comms/MOOSCommClient.h> // for CMOOSCommCl...
#include <MOOS/libMOOS/Comms/MOOSMsg.h>        // for CMOOSMsg
#include <boost/units/quantity.hpp>            // for operator/
#include <google/protobuf/descriptor.h>        // for Descriptor

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/coroner/groups.h"              // for health_report
#include "goby/middleware/protobuf/coroner.pb.h"         // for VehicleHealth
#include "goby/middleware/transport/interprocess.h"      // for InterProces...
#include "goby/moos/middleware/moos_plugin_translator.h" // for Translator
#include "goby/moos/moos_protobuf_helpers.h"             // for serialize_f...
#include "goby/moos/moos_translator.h"                   // for MOOSTranslator
#include "goby/util/debug_logger/flex_ostream.h"         // for operator<<
#include "goby/zeromq/application/multi_thread.h"        // for MultiThread...

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
