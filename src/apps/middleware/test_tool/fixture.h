// Copyright 2026:
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

#ifndef GOBY_APPS_MIDDLEWARE_TEST_TOOL_FIXTURE_H
#define GOBY_APPS_MIDDLEWARE_TEST_TOOL_FIXTURE_H

#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "goby/apps/middleware/test_tool/test_tool_config.pb.h"
#include "goby/middleware/protobuf/pubsub_test.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
/// \brief Signal number for "SIGINT", "INT", "2", etc.; 0 if unknown
int signal_from_name(const std::string& name);

/// \brief A command started in its own process group before the test and stopped after it
class Fixture
{
  public:
    Fixture(const protobuf::TestFixture& cfg, std::string log_path);
    ~Fixture();
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void start();

    /// \return true once ready; false (with reason()) on timeout, early exit or cancellation
    bool wait_ready(const std::function<bool()>& cancelled);

    /// \brief Signals the process group, escalating to SIGTERM and SIGKILL after the grace period
    void stop();

    /// \brief Stops reading output; call after every process that may hold the pipe is gone
    void close_output();

    const std::string& name() const { return cfg_.name(); }
    bool started() const { return pid_ != -1; }
    void fill_report(goby::middleware::protobuf::PubSubTestFixtureReport& report,
                     bool include_tail) const;

  private:
    void read_output();
    void update_exit_status();

    protobuf::TestFixture cfg_;
    std::string log_path_;
    pid_t pid_{-1};
    int out_fd_{-1};
    bool exited_{false};
    bool stopped_by_us_{false};
    int exit_status_{0};
    int exit_signal_{0};
    bool ready_{false};
    double ready_time_{0};
    std::string reason_;

    std::thread reader_;
    std::atomic<bool> stop_reading_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> tail_;
    bool matched_{false};
    std::unique_ptr<std::regex> ready_regex_;
};

/// \brief Fixtures for one test run: started in order, stopped in reverse
class FixtureSet
{
  public:
    FixtureSet(const google::protobuf::RepeatedPtrField<protobuf::TestFixture>& cfgs,
               std::string log_dir);
    ~FixtureSet();

    /// \return false if a fixture failed to start or become ready (later ones are not started)
    bool start_all(const std::function<bool()>& cancelled);

    /// \brief Stops every fixture, then any remaining descendant processes (e.g. daemonized
    /// screen sessions, on Linux), and reaps them
    void stop_all();

    void fill_reports(google::protobuf::RepeatedPtrField<
                          goby::middleware::protobuf::PubSubTestFixtureReport>& reports,
                      bool include_tail) const;

    const std::string& log_dir() const { return log_dir_; }

  private:
    std::vector<std::unique_ptr<Fixture>> fixtures_;
    std::string log_dir_;
    bool stopped_{false};
};

/// \brief Makes this process the reaper for orphaned descendants (Linux only; no-op elsewhere)
void enable_child_subreaper();

/// \brief Sends SIGTERM, then SIGKILL, to every remaining descendant and reaps them
void kill_descendants();

/// \brief Reaps any exited children, recording their statuses for Fixture
void reap_children();

} // namespace middleware
} // namespace apps
} // namespace goby

#endif
