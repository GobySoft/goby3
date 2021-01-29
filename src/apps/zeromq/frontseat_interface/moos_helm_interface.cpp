// Copyright 2020-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <ostream>
#include <vector>

#include <boost/units/quantity.hpp>

#include "goby/middleware/protobuf/app_config.pb.h"
#include "goby/moos/middleware/frontseat/frontseat_gateway_plugin.h"
#include "goby/moos/protobuf/moos_gateway_config.pb.h"
#include "goby/moos/protobuf/moos_helm_frontseat_interface_config.pb.h"
#include "goby/util/debug_logger/flex_ostream.h"
#include "goby/zeromq/protobuf/frontseat_interface_config.pb.h"

#include "frontseat_interface.h"

void goby::apps::zeromq::FrontSeatInterface::launch_helm_interface()
{
    if (cfg().HasExtension(goby::moos::protobuf::moos_helm))
    {
        goby::glog.is_verbose() && goby::glog << "Launching MOOS Helm interface thread"
                                              << std::endl;

        goby::apps::moos::protobuf::GobyMOOSGatewayConfig gateway_config;
        *gateway_config.mutable_app() = cfg().app();
        *gateway_config.mutable_moos() = cfg().GetExtension(goby::moos::protobuf::moos_helm);
        launch_thread<goby::moos::FrontSeatTranslation>(gateway_config);
    }
}
