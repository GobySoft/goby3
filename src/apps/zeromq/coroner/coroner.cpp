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

#include <algorithm>     // for copy
#include <chrono>        // for duration
#include <map>           // for operat...
#include <ostream>       // for basic_...
#include <ratio>         // for ratio
#include <set>           // for set
#include <string>        // for string
#include <type_traits>   // for __succ...
#include <unordered_map> // for operat...
#include <utility>       // for pair
#include <vector>        // for vector

#include <boost/units/systems/si/frequency.hpp> // for frequency

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/interface.h"            // for run
#include "goby/middleware/coroner/groups.h"                   // for health...
#include "goby/middleware/protobuf/coroner.pb.h"              // for Proces...
#include "goby/time/convert.h"                                // for conver...
#include "goby/time/system_clock.h"                           // for System...
#include "goby/time/types.h"                                  // for MicroTime
#include "goby/util/debug_logger.h"                           // for operat...
#include "goby/zeromq/application/single_thread.h"            // for Single...
#include "goby/zeromq/protobuf/coroner_config.pb.h"           // for Corone...
#include "goby/zeromq/protobuf/interprocess_config.pb.h"      // for InterP...
#include "goby/zeromq/transport/interprocess.h"               // for InterP...

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class Coroner : public goby::zeromq::SingleThreadApplication<protobuf::CoronerConfig>
{
  public:
    Coroner()
        : goby::zeromq::SingleThreadApplication<protobuf::CoronerConfig>(10.0 *
                                                                         boost::units::si::hertz),
          request_interval_(goby::time::convert_duration<decltype(request_interval_)>(
              cfg().request_interval_with_units())),
          response_timeout_(goby::time::convert_duration<decltype(response_timeout_)>(
              cfg().response_timeout_with_units()))
    {
        for (const std::string& expected : cfg().expected_name()) tracked_names_.insert(expected);

        interprocess()
            .subscribe<middleware::groups::health_response,
                       goby::middleware::protobuf::ProcessHealth>(
                [this](const goby::middleware::protobuf::ProcessHealth& response) {
                    glog.is_debug1() && glog << "Received response: " << response.ShortDebugString()
                                             << std::endl;
                    responses_[response.name()] = response;
                    if (!tracked_names_.count(response.name()))
                    {
                        glog.is_verbose() &&
                            glog << "Tracking new process name: " << response.name() << std::endl;
                        tracked_names_.insert(response.name());
                    }
                });
    }

    ~Coroner() override = default;

  private:
    void loop() override
    {
        auto now = goby::time::SystemClock::now();
        if (now >= last_request_time_ + request_interval_)
        {
            middleware::protobuf::HealthRequest request;
            interprocess().publish<middleware::groups::health_request>(request);
            last_request_time_ = now;
            waiting_for_response_ = true;
            responses_.clear();
        }

        if (waiting_for_response_ && now >= last_request_time_ + response_timeout_)
        {
            waiting_for_response_ = false;

            middleware::protobuf::VehicleHealth report;
            report.set_time_with_units(goby::time::SystemClock::now<goby::time::MicroTime>());
            goby::middleware::protobuf::HealthState health_state =
                goby::middleware::protobuf::HEALTH__OK;
            for (const std::string& expected : tracked_names_)
            {
                auto it = responses_.find(expected);

                if (it == responses_.end())
                {
                    glog.is_warn() && glog << "No response from: " << expected << std::endl;
                    health_state = goby::middleware::protobuf::HEALTH__FAILED;
                    auto& process = *report.add_process();
                    process.set_name(expected);
                    auto& main = *process.mutable_main();
                    main.set_name(expected);
                    main.set_state(goby::middleware::protobuf::HEALTH__FAILED);
                    main.set_error(goby::middleware::protobuf::ERROR__PROCESS_DIED);
                    main.set_error_message("Process " + expected + " has died");
                }
                else
                {
                    if (it->second.main().state() > health_state)
                        health_state = it->second.main().state();
                    *report.add_process() = it->second;
                }
            }

            report.set_platform(cfg().interprocess().platform());
            report.set_state(health_state);

            if (report.state() == goby::middleware::protobuf::HEALTH__OK)
            {
                glog.is_debug1() && glog << "Vehicle report: " << report.ShortDebugString()
                                         << std::endl;
            }
            else
            {
                glog.is_warn() && glog << "Vehicle report: " << report.ShortDebugString()
                                       << std::endl;
            }

            interprocess().publish<goby::middleware::groups::health_report>(report);
        }
    }

  private:
    goby::time::SystemClock::time_point last_request_time_{std::chrono::seconds(0)};
    goby::time::SystemClock::duration request_interval_;
    goby::time::SystemClock::duration response_timeout_;
    bool waiting_for_response_{false};

    std::map<std::string, goby::middleware::protobuf::ProcessHealth> responses_;

    std::set<std::string> tracked_names_;
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::zeromq::Coroner>(argc, argv); }
