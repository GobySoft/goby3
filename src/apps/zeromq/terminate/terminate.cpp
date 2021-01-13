// Copyright 2018-2020:
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

// for kill
#include <csignal>
#include <sys/types.h>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/protobuf/terminate.pb.h"
#include "goby/middleware/terminate/groups.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/terminate_config.pb.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class Terminate : public goby::zeromq::SingleThreadApplication<protobuf::TerminateConfig>
{
  public:
    Terminate()
        : goby::zeromq::SingleThreadApplication<protobuf::TerminateConfig>(10 *
                                                                           boost::units::si::hertz)
    {
        if (cfg().target_name_size() == 0 && cfg().target_pid_size() == 0)
            glog.is_die() &&
                glog << "Error, must specify at least one --target_name or --target_pid"
                     << std::endl;

        interprocess()
            .subscribe<middleware::groups::terminate_response,
                       goby::middleware::protobuf::TerminateResponse>(
                [this](const goby::middleware::protobuf::TerminateResponse& response) {
                    goby::middleware::protobuf::TerminateResult result;
                    result.set_target_name(response.target_name());
                    result.set_target_pid(response.target_pid());
                    result.set_result(
                        goby::middleware::protobuf::TerminateResult::PROCESS_RESPONDED);
                    interprocess().publish<middleware::groups::terminate_result>(result);

                    auto pid_it = waiting_for_response_pids_.find(response.target_pid());
                    if (pid_it != waiting_for_response_pids_.end())
                    {
                        glog.is_debug2() &&
                            glog << "Received terminate response from our target PID: "
                                 << response.target_pid() << std::endl;
                        waiting_for_response_pids_.erase(pid_it);
                        // insert the PID so we can track when it completely quits
                        running_pids_.insert(
                            std::make_pair(response.target_pid(), response.target_name()));
                    }

                    std::string target_name = response.target_name();
                    auto name_it = waiting_for_response_names_.find(target_name);
                    if (name_it != waiting_for_response_names_.end())
                    {
                        glog.is_debug2() && glog << "Received terminate response from our target: "
                                                 << target_name << std::endl;
                        waiting_for_response_names_.erase(name_it);
                        running_pids_.insert(
                            std::make_pair(response.target_pid(), response.target_name()));
                    }
                });

        for (const auto& target_name : cfg().target_name())
        {
            goby::middleware::protobuf::TerminateRequest req;
            req.set_target_name(target_name);
            waiting_for_response_names_.insert(target_name);
            glog.is_debug2() && glog << "Sending terminate request: " << req.ShortDebugString()
                                     << std::endl;
            interprocess().publish<middleware::groups::terminate_request>(req);
        }

        for (const auto& target_pid : cfg().target_pid())
        {
            goby::middleware::protobuf::TerminateRequest req;
            req.set_target_pid(target_pid);
            waiting_for_response_pids_.insert(target_pid);
            glog.is_debug2() && glog << "Sending terminate request: " << req.ShortDebugString()
                                     << std::endl;
            interprocess().publish<middleware::groups::terminate_request>(req);
        }
    }

    ~Terminate() {}

  private:
    void loop() override
    {
        // clear any processes that have exited
        for (auto it = running_pids_.begin(); it != running_pids_.end();)
        {
            if (!process_exists(it->first))
            {
                glog.is_debug2() && glog << "PID: " << it->first << " (was " << it->second
                                         << ") has quit." << std::endl;
                goby::middleware::protobuf::TerminateResult result;
                result.set_target_name(it->second);
                result.set_target_pid(it->first);
                result.set_result(
                    goby::middleware::protobuf::TerminateResult::PROCESS_CLEANLY_QUIT);
                interprocess().publish<middleware::groups::terminate_result>(result);
                running_pids_.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        // if we got a response from everything and it they have all quit, we're done
        if (waiting_for_response_names_.empty() && waiting_for_response_pids_.empty() &&
            running_pids_.empty())
        {
            glog.is_debug2() && glog << "All processes have cleanly quit." << std::endl;
            quit();
        }

        // check timeout
        decltype(start_time_) now(goby::time::SystemClock::now<time::MicroTime>()),
            timeout(cfg().response_timeout_with_units());
        if (now > start_time_ + timeout)
        {
            if (!waiting_for_response_names_.empty())
            {
                for (const auto& target_name : waiting_for_response_names_)
                {
                    goby::middleware::protobuf::TerminateResult result;
                    result.set_target_name(target_name);
                    result.set_result(
                        goby::middleware::protobuf::TerminateResult::TIMEOUT_RESPONSE);
                    interprocess().publish<middleware::groups::terminate_result>(result);
                }

                if (glog.is_warn())
                {
                    glog << "Timeout waiting for response from targets (by name): ";
                    for (const auto& target_name : waiting_for_response_names_)
                        glog << target_name << ", ";
                    glog << std::endl;
                }
            }
            if (!waiting_for_response_pids_.empty())
            {
                for (const auto& pid : waiting_for_response_pids_)
                {
                    goby::middleware::protobuf::TerminateResult result;
                    result.set_target_pid(pid);
                    result.set_result(
                        goby::middleware::protobuf::TerminateResult::TIMEOUT_RESPONSE);
                    interprocess().publish<middleware::groups::terminate_result>(result);
                }

                if (glog.is_warn())
                {
                    glog << "Timeout waiting for response from targets (by PID): ";
                    for (const auto& pid : waiting_for_response_pids_) glog << pid << ", ";
                    glog << std::endl;
                }
            }
            if (!running_pids_.empty())
            {
                for (const auto& p : running_pids_)
                {
                    goby::middleware::protobuf::TerminateResult result;
                    result.set_target_pid(p.first);
                    result.set_target_name(p.second);
                    result.set_result(goby::middleware::protobuf::TerminateResult::TIMEOUT_RUNNING);
                    interprocess().publish<middleware::groups::terminate_result>(result);
                }

                if (glog.is_warn())
                {
                    glog << "Timeout waiting for targets that responded to our request but have "
                            "not stopped running: ";
                    for (const auto& p : running_pids_)
                        glog << p.first << "(was " << p.second << "), ";
                    glog << std::endl;
                }
            }
            quit(EXIT_FAILURE);
        }
    }

    // If sig is 0, then no signal is sent, but error checking is still performed; this can be used to check for the existence of a process ID or process group ID.
    bool process_exists(unsigned pid) { return (0 == kill(pid, 0)); }

  private:
    time::MicroTime start_time_{goby::time::SystemClock::now<time::MicroTime>()};

    // targets that we requested by name that haven't responded
    std::set<std::string> waiting_for_response_names_;
    // targets that we requested by PID that haven't responded
    std::set<int> waiting_for_response_pids_;

    // targets that have responded but haven't quit yet (map of PID to former name)
    std::map<int, std::string> running_pids_;
}; // namespace zeromq
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::zeromq::Terminate>(argc, argv); }
