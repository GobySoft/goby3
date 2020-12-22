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

#include <gps.h>
#include <libgpsmm.h>

#include <boost/units/systems/angle/degrees.hpp>

#include <chrono>
#include <ctime>
#include <iostream>

#include "gpsd_client.h"

#include "goby/middleware/gpsd/groups.h"
#include "goby/time.h"
#include "goby/zeromq/application/single_thread.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
goby::apps::zeromq::GPSDClient::GPSDClient()
    : goby::zeromq::SingleThreadApplication<protobuf::GPSDConfig>()
{
    if (cfg().device_size())
    {
        for (auto& dev : cfg().device())
        {
            fix_map_[dev.device_name()] = GPSFixData();
            fix_map_[dev.device_name()].name = dev.device_name();
            fix_map_[dev.device_name()].trigger_mask = (1 << dev.trigger_field());
        }
    }
    else
    {
        // Should be initialized with defaults.
        protobuf::GPSTrigger dev;
        fix_map_[dev.device_name()] = GPSFixData();
        fix_map_[dev.device_name()].trigger_mask = (1 << dev.trigger_field());

        glog.is_warn() && glog << "No devices configured. Using defaults." << std::endl;
    }

    gpsmm gps_rec(cfg().hostname().c_str(), cfg().port().c_str());

    // compare devices from our config to what gps_rec has in it's list.
    if (gps_rec.stream(WATCH_ENABLE) == NULL)
    {
        glog.is_die() && glog << "No GPSD running.\n";
    }

    while (true)
    {
        struct gps_data_t* gps_data;

        if (!gps_rec.waiting(1000))
        {
            // Notify data missing?
            continue;
        }

        if ((gps_data = gps_rec.read()) == NULL)
        {
            glog.is_die() && glog << "Read error..\n";
        }
        else
        {
            ////// GPS Fix Data vvvvvv
            GPSFixData& fix_data = fix_map_[gps_data->dev.path];
            gps_fix_t* gps_fix_ptr = &fix_data.merged_fix;
            gps_merge_fix(gps_fix_ptr, gps_data->set, &gps_data->fix);
            fix_data.merged_mask = fix_data.merged_mask | gps_data->set;

            if (fix_data.is_ready())
            {
                fix_data.build_data_to_publish();
                if (fix_data.fix.IsInitialized())
                {
                    interprocess().publish<goby::middleware::groups::gps_fix>(fix_data.fix);
                    fix_data.set_data_as_published();
                }
                else
                {
                    glog.is_debug1() && glog << "Protobuf is not ready to publish" << std::endl;
                }
            }

            ////// GPS Fix Data ^^^^^^

            ////// GPS Attitude Data vvvvvv

            ////// GPS Attitude Data ^^^^^^
        }
    }
}

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::zeromq::GPSDClient>(argc, argv); }
