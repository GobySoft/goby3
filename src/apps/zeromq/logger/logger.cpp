// Copyright 2017-2022:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Ryan Govostes <rgovostes+git@gmail.com>
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

#include <algorithm>     // for copy, max
#include <atomic>        // for atomic
#include <chrono>        // for time_p...
#include <csignal>       // for sigaction
#include <dlfcn.h>       // for dlclose
#include <fcntl.h>       // for S_IRGRP
#include <fstream>       // for operat...
#include <functional>    // for _Bind
#include <map>           // for operat...
#include <string>        // for allocator
#include <sys/stat.h>    // for chmod
#include <thread>        // for thread
#include <unordered_map> // for operat...
#include <vector>        // for vector

#include <boost/units/quantity.hpp>             // for operator*
#include <boost/units/systems/si/frequency.hpp> // for frequency

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/interface.h"            // for run
#include "goby/middleware/group.h"                            // for operat...
#include "goby/middleware/log/dccl_log_plugin.h"              // for DCCLPl...
#include "goby/middleware/log/groups.h"
#include "goby/middleware/log/log_entry.h"           // for LogEntry
#include "goby/middleware/log/protobuf_log_plugin.h" // for Protob...
#include "goby/middleware/marshalling/interface.h"   // for Marsha...
#include "goby/middleware/protobuf/logger.pb.h"
#include "goby/time/convert.h"                           // for file_str
#include "goby/util/debug_logger/flex_ostream.h"         // for operat...
#include "goby/zeromq/application/single_thread.h"       // for Single...
#include "goby/zeromq/protobuf/interprocess_config.pb.h" // for InterP...
#include "goby/zeromq/protobuf/logger_config.pb.h"       // for Logger...
#include "goby/zeromq/transport/interprocess.h"          // for InterP...

using goby::glog;

void signal_handler(int sig);

namespace goby
{
namespace apps
{
namespace zeromq
{
class Logger : public goby::zeromq::SingleThreadApplication<protobuf::LoggerConfig>
{
  public:
    Logger()
        : goby::zeromq::SingleThreadApplication<protobuf::LoggerConfig>(1 *
                                                                        boost::units::si::hertz),
          log_file_base_(std::string(cfg().log_dir() + "/" + cfg().interprocess().platform() + "_"))
    {
        open_log();

        logging_ = cfg().log_at_startup();

        namespace sp = std::placeholders;
        interprocess().subscribe_regex(
            std::bind(&Logger::log, this, sp::_1, sp::_2, sp::_3, sp::_4),
            {goby::middleware::MarshallingScheme::ALL_SCHEMES}, cfg().type_regex(),
            cfg().group_regex());

        for (const auto& lib : cfg().load_shared_library())
        {
            void* lib_handle = dlopen(lib.c_str(), RTLD_LAZY);
            if (!lib_handle)
                glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
            dl_handles_.push_back(lib_handle);
        }

        interprocess().subscribe<goby::middleware::groups::logger_request>(
            [this](const goby::middleware::protobuf::LoggerRequest& request) {
                switch (request.requested_state())
                {
                    case goby::middleware::protobuf::LoggerRequest::START_LOGGING:
                        if (logging_)
                            glog.is_warn() &&
                                glog << "Received START_LOGGING but we are already logging"
                                     << std::endl;
                        else
                            glog.is_debug1() && glog << "Logging started" << std::endl;

                        logging_ = true;
                        break;

                    case goby::middleware::protobuf::LoggerRequest::STOP_LOGGING:
                        if (!logging_)
                            glog.is_warn() &&
                                glog << "Received STOP_LOGGING but we were already stopped"
                                     << std::endl;
                        else
                            glog.is_debug1() && glog << "Logging stopped" << std::endl;

                        logging_ = false;
                        break;

                    case goby::middleware::protobuf::LoggerRequest::ROTATE_LOG:
                        glog.is_debug1() && glog << "Log rotated" << std::endl;
                        close_log();
                        open_log();
                        break;
                }
            });
    }

    ~Logger() override
    {
        close_log();

        for (void* handle : dl_handles_) dlclose(handle);
    }

    static std::atomic<bool> do_quit;

  private:
    void open_log()
    {
        pb_plugin_.reset(new goby::middleware::log::ProtobufPlugin);
        dccl_plugin_.reset(new goby::middleware::log::DCCLPlugin);

        log_file_path_ = log_file_base_ + goby::time::file_str() + ".goby";
        log_.reset(new std::ofstream(log_file_path_.c_str(), std::ofstream::binary));

        if (!log_->is_open())
            glog.is_die() && glog << "Failed to open log in directory: " << cfg().log_dir()
                                  << std::endl;
        else
            glog.is_verbose() && glog << "Logging to: " << log_file_path_ << std::endl;

        pb_plugin_->register_write_hooks(*log_);
        dccl_plugin_->register_write_hooks(*log_);

        std::string file_symlink = log_file_base_ + "latest.goby";
        remove(file_symlink.c_str());
        int result = symlink(realpath(log_file_path_.c_str(), NULL), file_symlink.c_str());
        if (result != 0)
            glog.is_warn() &&
                glog << "Cannot create symlink to latest file. Continuing onwards anyway"
                     << std::endl;
    }

    void close_log()
    {
        glog.is_verbose() && glog << "Closing log at: " << log_file_path_ << std::endl;
        log_->close();
        log_.reset();
        goby::middleware::log::LogEntry::reset();
        
        pb_plugin_.reset();
        dccl_plugin_.reset();

        // set read only
        chmod(log_file_path_.c_str(), S_IRUSR | S_IRGRP);
    }

    void log(const std::vector<unsigned char>& data, int scheme, const std::string& type,
             const goby::middleware::Group& group);
    void loop() override
    {
        if (do_quit)
            quit();
    }

  private:
    std::string log_file_base_;
    std::string log_file_path_;
    std::unique_ptr<std::ofstream> log_;

    std::vector<void*> dl_handles_;

    std::unique_ptr<goby::middleware::log::ProtobufPlugin> pb_plugin_;
    std::unique_ptr<goby::middleware::log::DCCLPlugin> dccl_plugin_;

    bool logging_{true};
};
} // namespace zeromq
} // namespace apps
} // namespace goby

std::atomic<bool> goby::apps::zeromq::Logger::do_quit{false};

int main(int argc, char* argv[])
{
    // block signals from all but this main thread
    sigset_t new_mask;
    sigfillset(&new_mask);
    sigset_t old_mask;
    pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

    std::thread t([&argc, &argv]() { goby::run<goby::apps::zeromq::Logger>(argc, argv); });

    // unblock signals
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    pthread_sigmask(SIG_SETMASK, &empty_mask, nullptr);

    struct sigaction action;
    action.sa_handler = &signal_handler;

    // register the usual quitting signals
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGQUIT, &action, nullptr);

    // wait for the app to quit
    t.join();

    return 0;
}

void signal_handler(int /*sig*/) { goby::apps::zeromq::Logger::do_quit = true; }

void goby::apps::zeromq::Logger::log(const std::vector<unsigned char>& data, int scheme,
                                     const std::string& type, const goby::middleware::Group& group)
{
    if (!logging_)
        return;

    glog.is_debug1() && glog << "Received " << data.size()
                             << " bytes to log to [scheme, type, group] = [" << scheme << ", "
                             << type << ", " << group << "]" << std::endl;

    goby::middleware::log::LogEntry entry(data, scheme, type, group);
    entry.serialize(&*log_);
}
