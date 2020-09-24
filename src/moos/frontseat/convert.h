#pragma once

#include "goby/middleware/protobuf/frontseat_data.pb.h"
#include "goby/moos/moos_header.h"

namespace goby
{
namespace moos
{
void convert_and_publish_node_status(
    const goby::middleware::frontseat::protobuf::NodeStatus& status, CMOOSCommClient& moos_comms);
}
} // namespace goby
