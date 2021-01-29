// Copyright 2012-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef GOBY_UTIL_DEBUG_LOGGER_FLEX_OSTREAMBUF_H
#define GOBY_UTIL_DEBUG_LOGGER_FLEX_OSTREAMBUF_H

#include <atomic>
#include <cstdio>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>

#include <thread>

#include <boost/date_time.hpp>
#include <memory>

#include "goby/util/protobuf/debug_logger.pb.h"

#include "term_color.h"

namespace goby
{
namespace util
{
class FlexNCurses;

namespace logger
{
class Group;
}

namespace logger_lock
{
/// Mutex actions available to the Goby logger (glogger)
enum LockAction
{
    none,
    lock
};
} // namespace logger_lock

namespace logger
{
extern std::recursive_mutex mutex;

enum Verbosity
{
    UNKNOWN = 3,
    QUIET = protobuf::GLogConfig::QUIET,
    WARN = protobuf::GLogConfig::WARN,
    VERBOSE = protobuf::GLogConfig::VERBOSE,
    //GUI = protobuf::GLogConfig::GUI,
    DEBUG1 = protobuf::GLogConfig::DEBUG1,
    DEBUG2 = protobuf::GLogConfig::DEBUG2,
    DEBUG3 = protobuf::GLogConfig::DEBUG3,
    DIE = -1
};
}; // namespace logger

/// Class derived from std::stringbuf that allows us to insert things before the stream and control output. This is the string buffer used by goby::util::FlexOstream for the Goby Logger (glogger)
class FlexOstream;

class FlexOStreamBuf : public std::streambuf
{
  public:
    FlexOStreamBuf(FlexOstream* parent);
    ~FlexOStreamBuf();

    /// virtual inherited from std::streambuf. Called when std::endl or std::flush is inserted into the stream
    int sync();

    /// virtual inherited from std::streambuf. Called when something is inserted into the stream
    int overflow(int c = EOF);

    /// name of the application being served
    void name(const std::string& s) { name_ = s; }

    /// add a stream to the logger
    void add_stream(logger::Verbosity verbosity, std::ostream* os);

    /// do all attached streams have Verbosity == quiet?
    bool is_quiet() const { return highest_verbosity_ == logger::QUIET; }

    /// is there an attached stream with Verbosity == gui (ncurses GUI)
    bool is_gui() const { return is_gui_; }

    void enable_gui();

    logger::Verbosity highest_verbosity() const { return highest_verbosity_; }

    /// current group name (last insertion of group("") into the stream)
    void group_name(const std::string& s) { group_name_ = s; }

    /// exit on error at the next call to sync()
    void set_die_flag(bool b) { die_flag_ = b; }

    void set_verbosity_depth(logger::Verbosity depth) { current_verbosity_ = depth; }

    logger::Verbosity verbosity_depth() { return current_verbosity_; }

    /// add a new group
    void add_group(const std::string& name, logger::Group g);

    /// refresh the display (does nothing if !is_gui())
    void refresh();

    void set_lock_action(logger_lock::LockAction lock_action) { lock_action_ = lock_action; }

    logger_lock::LockAction lock_action() { return lock_action_; }

  private:
    void display(std::string& s);
    void strip_escapes(std::string& s);

  private:
    std::deque<std::string> buffer_;

    class StreamConfig
    {
      public:
        StreamConfig(std::ostream* os, logger::Verbosity verbosity) : os_(os), verbosity_(verbosity)
        {
        }

        void set_verbosity(logger::Verbosity verbosity) { verbosity_ = verbosity; }

        std::ostream* os() const { return os_; }
        logger::Verbosity verbosity() const { return verbosity_; }

      private:
        std::ostream* os_;
        logger::Verbosity verbosity_;
    };

    std::string name_;
    std::string group_name_;

    std::map<std::string, logger::Group> groups_;

    std::atomic<bool> die_flag_;
    std::atomic<logger::Verbosity> current_verbosity_;

    FlexNCurses* curses_;
    std::shared_ptr<std::thread> input_thread_;

    boost::posix_time::ptime start_time_;

    std::vector<StreamConfig> streams_;

    bool is_gui_;

    std::atomic<logger::Verbosity> highest_verbosity_;

    std::atomic<logger_lock::LockAction> lock_action_;
    //    FlexOstream* parent_;
};
} // namespace util
} // namespace goby
#endif
