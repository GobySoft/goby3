/*
 * gpsd_client.h
 * Copyright (C) 2020 Shawn Dooley <shawn@shawndooley.net>
 *
 * Distributed under terms of the MIT license.
 */

#pragma once

#include <gps.h>

#include <map>
#include <string>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/zeromq/application/single_thread.h"

#include "goby/middleware/protobuf/gpsd.pb.h"
#include "goby/zeromq/protobuf/gps_config.pb.h"
#include "gps_fix_data.h"

namespace goby
{
namespace apps
{
namespace zeromq
{
class GPSDClient : public goby::zeromq::SingleThreadApplication<protobuf::GPSDConfig>
{
  public:
    GPSDClient();

  private:
    std::map<std::string, GPSFixData> fix_map_;
};
} // namespace zeromq
} // namespace apps
} // namespace goby
