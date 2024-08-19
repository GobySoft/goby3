// Copyright 2012-2023:
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

#include <algorithm> // for copy, max
#include <atomic>    // for atomic
#include <cassert>   // for assert
#include <chrono>    // for time_point
#include <cstdio>    // for EOF
#include <cstdlib>   // for exit
#include <deque>     // for deque
#include <iomanip>   // for operator<<
#include <iostream>  // for operator<<
#include <iterator>  // for ostreamb...
#include <map>       // for map, map...
#include <memory>    // for make_shared
#include <mutex>     // for mutex
#include <sstream>   // for basic_st...
#include <string>    // for string
#include <thread>    // for thread
#include <utility>   // for move, pair
#include <vector>    // for vector

#include <boost/date_time/gregorian/gregorian.hpp>          // for date
#include <boost/date_time/posix_time/posix_time_config.hpp> // for time_dur...
#include <boost/date_time/posix_time/posix_time_io.hpp>     // for operator<<
#include <boost/date_time/posix_time/ptime.hpp>             // for ptime

#include "goby/exception.h"
#include "goby/time/convert.h"                      // for SystemCl...
#include "goby/time/system_clock.h"                 // for SystemClock
#include "goby/util/debug_logger/flex_ostreambuf.h" // for FlexOStr...
#include "goby/util/debug_logger/term_color.h"      // for esc_nocolor

#ifdef HAS_NCURSES
#include "flex_ncurses.h" // for FlexNCurses
#endif
#include "flex_ostream.h"        // for FlexOstream
#include "logger_manipulators.h" // for Group

using goby::time::SystemClock;

#ifdef HAS_NCURSES
std::mutex curses_mutex;
#endif

std::recursive_mutex goby::util::logger::mutex;

goby::util::FlexOStreamBuf::FlexOStreamBuf(FlexOstream* parent)
    : buffer_(1),
      name_("no name"),
      die_flag_(false),
      current_verbosity_(logger::UNKNOWN),
      curses_(nullptr),
      start_time_(time::SystemClock::now<boost::posix_time::ptime>()),
      is_gui_(false),
      highest_verbosity_(logger::QUIET)
//      parent_(parent)

{
    logger::Group no_group("", "Ungrouped messages");
    groups_[""] = no_group;
}

goby::util::FlexOStreamBuf::~FlexOStreamBuf()
{
#ifdef HAS_NCURSES
    if (curses_)
        delete curses_;
#endif
}

void goby::util::FlexOStreamBuf::add_stream(logger::Verbosity verbosity, std::ostream* os)
{
    //check that this stream doesn't exist
    // if so, update its verbosity and return
    bool stream_exists = false;
    for (StreamConfig& sc : streams_)
    {
        if (sc.os() == os)
        {
            sc.set_verbosity(verbosity);
            stream_exists = true;
        }
    }

    if (!stream_exists)
        streams_.emplace_back(os, verbosity);

    highest_verbosity_ = logger::QUIET;
    for (auto stream : streams_)
    {
        if (stream.verbosity() > highest_verbosity_)
            highest_verbosity_ = stream.verbosity();
    }
}

void goby::util::FlexOStreamBuf::remove_stream(std::ostream* os)
{
    streams_.erase(std::remove_if(streams_.begin(), streams_.end(),
                                  [&os](const StreamConfig& sc) { return sc.os() == os; }));

    highest_verbosity_ = logger::QUIET;
    for (auto stream : streams_)
    {
        if (stream.verbosity() > highest_verbosity_)
            highest_verbosity_ = stream.verbosity();
    }
}

void goby::util::FlexOStreamBuf::enable_gui()
{
#ifdef HAS_NCURSES

    is_gui_ = true;
    curses_ = new FlexNCurses;

    std::lock_guard<std::mutex> lock(curses_mutex);

    curses_->startup();

    // add any groups already on the screen as ncurses windows
    for (const auto& p : groups_) curses_->add_win(&groups_[p.first]);

    curses_->recalculate_win();

    input_thread_ = std::make_shared<std::thread>([&]() { curses_->run_input(); });
#else
    // suppress -Wunused-private-field
    (void)curses_;

    throw(goby::Exception("Tried to enable NCurses GUI without compiling against NCurses. Install "
                          "NCurses and recompile goby or disable GUI functionality"));
#endif
}

void goby::util::FlexOStreamBuf::add_group(const std::string& name, logger::Group g)
{
    bool group_existed = groups_.count(name);

    groups_[name] = std::move(g);

#ifdef HAS_NCURSES
    // only create a new window if this group didn't exist before
    if (is_gui_ && !group_existed)
    {
        std::lock_guard<std::mutex> lock(curses_mutex);
        curses_->add_win(&groups_[name]);
    }
#endif
}

int goby::util::FlexOStreamBuf::overflow(int c /*= EOF*/)
{
    //    parent_->set_unset_verbosity();

    if (c == EOF)
        return c;
    else if (c == '\n')
        buffer_.emplace_back();
    else
        buffer_.back().push_back(c);

    return c;
}

// called when flush() or std::endl
int goby::util::FlexOStreamBuf::sync()
{
    if (current_verbosity_ == logger::UNKNOWN && lock_action_ == logger_lock::lock)
    {
        std::cerr
            << "== Misuse of goby::glog in threaded mode: must use 'glog.is_*() && glog' syntax. "
               "For example, glog.is_verbose() && glog << \"My message\" << std::endl;"
            << std::endl;
        std::cerr << "== Offending line: " << buffer_.front() << std::endl;
        assert(!(lock_action_ == logger_lock::lock && current_verbosity_ == logger::UNKNOWN));
        exit(EXIT_FAILURE);
        return 0;
    }

    // all but last one
    while (buffer_.size() > 1)
    {
        display(buffer_.front());
        buffer_.pop_front();
    }

    group_name_.erase();

    current_verbosity_ = logger::UNKNOWN;

    if (lock_action_ == logger_lock::lock)
    {
        logger::mutex.unlock();
    }

    if (die_flag_)
        exit(EXIT_FAILURE);

    return 0;
}

void goby::util::FlexOStreamBuf::display(std::string& s)
{
    bool gui_displayed = false;
    for (const StreamConfig& cfg : streams_)
    {
        if ((cfg.os() == &std::cout || cfg.os() == &std::cerr || cfg.os() == &std::clog) &&
            current_verbosity_ <= cfg.verbosity())
        {
#ifdef HAS_NCURSES
            if (is_gui_ && current_verbosity_ <= cfg.verbosity() && !gui_displayed)
            {
                if (!die_flag_)
                {
                    std::lock_guard<std::mutex> lock(curses_mutex);
                    std::stringstream line;
                    boost::posix_time::time_duration time_of_day =
                        SystemClock::now<boost::posix_time::ptime>().time_of_day();
                    line << "\n"
                         << std::setfill('0') << std::setw(2) << time_of_day.hours() << ":"
                         << std::setw(2) << time_of_day.minutes() << ":" << std::setw(2)
                         << time_of_day.seconds()
                         << TermColor::esc_code_from_col(groups_[group_name_].color()) << " | "
                         << esc_nocolor << s;

                    curses_->insert(SystemClock::now<boost::posix_time::ptime>(), line.str(),
                                    &groups_[group_name_]);
                }
                else
                {
                    curses_->alive(false);
                    input_thread_->join();
                    curses_->cleanup();
                    std::cerr << TermColor::esc_code_from_col(groups_[group_name_].color()) << name_
                              << esc_nocolor << ": " << s << esc_nocolor << std::endl;
                }
                gui_displayed = true;
                continue;
            }
#else
            // suppress -Wunused-but-set-variable
            (void)gui_displayed;
#endif

            *cfg.os() << TermColor::esc_code_from_col(groups_[group_name_].color()) << name_
                      << esc_nocolor << " [" << goby::time::str() << "]";
            if (!group_name_.empty())
                *cfg.os() << " "
                          << "{" << group_name_ << "}";
            *cfg.os() << ": " << s << std::endl;
        }
        else if (cfg.os() && current_verbosity_ <= cfg.verbosity())
        {
            goby::util::logger::basic_log_header(*cfg.os(), group_name_);
            strip_escapes(s);
            *cfg.os() << s << std::endl;
        }
    }
}

void goby::util::FlexOStreamBuf::refresh()
{
#ifdef HAS_NCURSES
    if (is_gui_)
    {
        std::lock_guard<std::mutex> lock(curses_mutex);
        curses_->recalculate_win();
    }
#endif
}

// clean out any escape codes for non terminal streams
void goby::util::FlexOStreamBuf::strip_escapes(std::string& s)
{
    static const std::string esc = "\33[";
    static const std::string m = "m";

    size_t esc_pos, m_pos;
    while ((esc_pos = s.find(esc)) != std::string::npos &&
           (m_pos = s.find(m, esc_pos)) != std::string::npos)
        s.erase(esc_pos, m_pos - esc_pos + 1);
}
