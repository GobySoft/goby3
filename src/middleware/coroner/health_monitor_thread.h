// Copyright 2017-2022:
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

#ifndef GOBY_MIDDLEWARE_CORONER_HEALTH_MONITOR_THREAD_H
#define GOBY_MIDDLEWARE_CORONER_HEALTH_MONITOR_THREAD_H

#include "goby/middleware/coroner/groups.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/protobuf/coroner.pb.h"

#include "goby/middleware/application/simple_thread.h"

namespace goby
{
namespace middleware
{
struct NullConfig
{
};

class HealthMonitorThread : public SimpleThread<NullConfig>
{
  public:
    HealthMonitorThread();

  private:
    void loop() override;
    void initialize() override { this->set_name("health_monitor"); }

  private:
    protobuf::ProcessHealth health_response_;
    // uid to response
    std::map<int, std::shared_ptr<const protobuf::ThreadHealth>> child_responses_;
    goby::time::SteadyClock::time_point last_health_request_time_;
    const goby::time::SteadyClock::duration health_request_timeout_{std::chrono::seconds(1)};
    bool waiting_for_responses_{false};
};

} // namespace middleware
} // namespace goby

#endif
