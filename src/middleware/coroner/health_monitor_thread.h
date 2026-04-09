// Copyright 2017-2026:
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

template <typename ImplementationTag>
class HealthMonitorThread : public SimpleThread<NullConfig, ImplementationTag>
{
  public:
    HealthMonitorThread()
        : SimpleThread<NullConfig, ImplementationTag>(NullConfig(), 1.0 * boost::units::si::hertz)
    {
        // handle goby_coroner request
        this->interprocess().template subscribe<groups::health_request, protobuf::HealthRequest>(
            [this](const protobuf::HealthRequest& request)
            {
                this->interthread().template publish<groups::health_request>(
                    protobuf::HealthRequest());
                waiting_for_responses_ = true;
                last_health_request_time_ = goby::time::SteadyClock::now();

                std::shared_ptr<protobuf::ThreadHealth> our_response(new protobuf::ThreadHealth);
                this->thread_health(*our_response);
                child_responses_[our_response->uid()] = our_response;
            });

        // handle response from main thread
        this->interthread().template subscribe<groups::health_response>(
            [this](std::shared_ptr<const protobuf::ProcessHealth> response)
            { health_response_ = *response; });

        // handle response from child threads
        this->interthread().template subscribe<groups::health_response>(
            [this](std::shared_ptr<const protobuf::ThreadHealth> response)
            { child_responses_[response->uid()] = response; });
    }

  private:
    void loop() override
    {
        if (waiting_for_responses_ &&
            goby::time::SteadyClock::now() > last_health_request_time_ + health_request_timeout_)
        {
            goby::middleware::protobuf::HealthState health_state = health_response_.main().state();

            // overwrite any child responses we got
            for (auto& thread_health : *health_response_.mutable_main()->mutable_child())
            {
                if (child_responses_.count(thread_health.uid()))
                    thread_health = *child_responses_.at(thread_health.uid());

                if (thread_health.state() > health_state)
                    health_state = thread_health.state();
            }

            health_response_.mutable_main()->set_state(health_state);

            if (health_response_.IsInitialized())
                this->interprocess().template publish<groups::health_response>(health_response_);

            waiting_for_responses_ = false;
            child_responses_.clear();
            health_response_.Clear();
        }
    }
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
