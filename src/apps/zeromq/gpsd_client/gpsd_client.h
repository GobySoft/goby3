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

#ifndef GOBY_APPS_ZEROMQ_GPSD_CLIENT_GPSD_CLIENT_H
#define GOBY_APPS_ZEROMQ_GPSD_CLIENT_GPSD_CLIENT_H

#include <gps.h>

#include <map>
#include <string>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/zeromq/application/single_thread.h"

#include "goby/middleware/protobuf/gpsd.pb.h"
#include "goby/zeromq/protobuf/gps_config.pb.h"

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
    void loop() override;

    void handle_tpv(nlohmann::json& data);
    void handle_sky(nlohmann::json& data);
    void handle_att(nlohmann::json& data);

  private:
    gpsmm gps_rec_;

    std::set<std::string> device_list_;
    bool publish_all_{false};
};
} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
