// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
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

#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>

#include "goby/common/time.h"
#include "goby/middleware/log.h"
#include "goby/middleware/single-thread-application.h"

#include "goby/middleware/protobuf/logger_config.pb.h"

using goby::glog;
using namespace goby::common::logger;

void signal_handler(int sig);

namespace goby
{
class Logger : public goby::SingleThreadApplication<protobuf::LoggerConfig>
{
  public:
    Logger()
        : goby::SingleThreadApplication<protobuf::LoggerConfig>(1 * boost::units::si::hertz),
          log_file_path_(std::string(cfg().log_dir() + "/" + cfg().interprocess().platform() + "_" +
                                     goby::common::goby_file_timestamp() + ".goby")),
          log_(log_file_path_.c_str(), std::ofstream::binary)
    {
        if (!log_.is_open())
            glog.is(DIE) && glog << "Failed to open log in directory: " << cfg().log_dir()
                                 << std::endl;

        namespace sp = std::placeholders;
        interprocess().subscribe_regex(
            std::bind(&Logger::log, this, sp::_1, sp::_2, sp::_3, sp::_4),
            {goby::MarshallingScheme::ALL_SCHEMES}, cfg().type_regex(), cfg().group_regex());
    }

    ~Logger()
    {
        log_.close();
        // set read only
        chmod(log_file_path_.c_str(), S_IRUSR | S_IRGRP);
    }

    void log(const std::vector<unsigned char>& data, int scheme, const std::string& type,
             const Group& group);
    void loop() override
    {
        if (do_quit)
            quit();
    }

    static std::atomic<bool> do_quit;

  private:
    std::string log_file_path_;
    std::ofstream log_;
};
} // namespace goby

std::atomic<bool> goby::Logger::do_quit{false};

int main(int argc, char* argv[])
{
    // block signals from all but this main thread
    sigset_t new_mask;
    sigfillset(&new_mask);
    sigset_t old_mask;
    pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

    std::thread t([&argc, &argv]() { goby::run<goby::Logger>(argc, argv); });

    // unblock signals
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    pthread_sigmask(SIG_SETMASK, &empty_mask, 0);

    struct sigaction action;
    action.sa_handler = &signal_handler;

    // register the usual quitting signals
    sigaction(SIGINT, &action, 0);
    sigaction(SIGTERM, &action, 0);
    sigaction(SIGQUIT, &action, 0);

    // wait for the app to quit
    t.join();

    return 0;
}

void signal_handler(int sig) { goby::Logger::do_quit = true; }

void goby::Logger::log(const std::vector<unsigned char>& data, int scheme, const std::string& type,
                       const Group& group)
{
    glog.is(DEBUG1) && glog << "Received " << data.size()
                            << " bytes to log to [scheme, type, group] = [" << scheme << ", "
                            << type << ", " << group << "]" << std::endl;

    LogEntry entry(data, scheme, type, group);

    // TODO: add logger hook
    // plugin.log(entry);

    entry.serialize(&log_);
}
