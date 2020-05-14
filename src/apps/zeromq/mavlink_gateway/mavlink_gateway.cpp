// Copyright 2019-2020:
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

#include <cmath>
#include <string>
#include <vector>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/io/serial_mavlink.h"
#include "goby/middleware/io/udp_mavlink.h"

#include "goby/zeromq/application/multi_thread.h"
#include "goby/zeromq/protobuf/mavlink_gateway_config.pb.h"

using AppBase =
    goby::zeromq::MultiThreadApplication<goby::apps::zeromq::protobuf::MAVLinkGatewayConfig>;
using ThreadBase =
    goby::middleware::SimpleThread<goby::apps::zeromq::protobuf::MAVLinkGatewayConfig>;
namespace si = boost::units::si;

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class MAVLinkGateway : public AppBase
{
  public:
    using SerialThread =
        goby::middleware::io::SerialThreadMAVLink<goby::middleware::io::groups::mavlink_raw_in,
                                                  goby::middleware::io::groups::mavlink_raw_out,
                                                  goby::middleware::io::PubSubLayer::INTERPROCESS,
                                                  goby::middleware::io::PubSubLayer::INTERPROCESS>;

    using UDPThread =
        goby::middleware::io::UDPThreadMAVLink<goby::middleware::io::groups::mavlink_raw_in,
                                               goby::middleware::io::groups::mavlink_raw_out,
                                               goby::middleware::io::PubSubLayer::INTERPROCESS,
                                               goby::middleware::io::PubSubLayer::INTERPROCESS>;

    MAVLinkGateway()
    {
        interprocess()
            .subscribe<goby::middleware::io::groups::mavlink_raw_in,
                       std::tuple<int, int, mavlink::common::msg::HEARTBEAT>>(
                [](const std::tuple<int, int, mavlink::common::msg::HEARTBEAT>& hb_with_metadata) {
                    int sysid, compid;
                    mavlink::common::msg::HEARTBEAT hb;
                    std::tie(sysid, compid, hb) = hb_with_metadata;
                    goby::glog.is_debug1() && goby::glog << "Received heartbeat [sysid: " << sysid
                                                         << ", compid: " << compid
                                                         << "]: " << hb.to_yaml() << std::endl;
                });

        switch (cfg().connection_type())
        {
            case protobuf::MAVLinkGatewayConfig::CONNECTION_SERIAL:
                launch_thread<SerialThread>(cfg().serial());
                break;
            case protobuf::MAVLinkGatewayConfig::CONNECTION_UDP:
                launch_thread<UDPThread>(cfg().udp());
                break;
        }
    }
};

class MAVLinkGatewayConfigurator : public goby::middleware::ProtobufConfigurator<
                                       goby::apps::zeromq::protobuf::MAVLinkGatewayConfig>
{
  public:
    MAVLinkGatewayConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<
              goby::apps::zeromq::protobuf::MAVLinkGatewayConfig>(argc, argv)
    {
        goby::apps::zeromq::protobuf::MAVLinkGatewayConfig& cfg = mutable_cfg();

        if (cfg.connection_type() == protobuf::MAVLinkGatewayConfig::CONNECTION_SERIAL)
        {
            goby::middleware::protobuf::SerialConfig& serial_cfg = *cfg.mutable_serial();
            // set default baud
            if (!serial_cfg.has_baud())
                serial_cfg.set_baud(57600);
        }
    }
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::MAVLinkGateway>(
        goby::apps::zeromq::MAVLinkGatewayConfigurator(argc, argv));
}
