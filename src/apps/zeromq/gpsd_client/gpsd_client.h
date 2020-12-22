// Copyright 2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Shawn Dooley <shawn@shawndooley.net>
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
