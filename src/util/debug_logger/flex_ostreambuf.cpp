// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include <boost/date_time.hpp>

#include "goby/util/debug_logger/flex_ostreambuf.h"

#ifdef HAS_NCURSES
#include "flex_ncurses.h"
#endif
#include "flex_ostream.h"

#include "goby/exception.h"
#include "goby/time.h"
#include "goby/util/sci.h"
#include "logger_manipulators.h"

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
      curses_(0),
      start_time_(time::SystemClock::now<boost::posix_time::ptime>()),
      is_gui_(false),
      highest_verbosity_(logger::QUIET),
      parent_(parent)

{
    Group no_group("", "Ungrouped messages");
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
        streams_.push_back(StreamConfig(os, verbosity));

    highest_verbosity_ = logger::QUIET;
    for (std::vector<StreamConfig>::const_iterator it = streams_.begin(), end = streams_.end();
         it != end; ++it)
    {
        if (it->verbosity() > highest_verbosity_)
            highest_verbosity_ = it->verbosity();
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
    typedef std::pair<std::string, Group> P;
    for (const P& p : groups_) curses_->add_win(&groups_[p.first]);

    curses_->recalculate_win();

    input_thread_ = std::shared_ptr<std::thread>(new std::thread([&]() { curses_->run_input(); }));
#else
    // suppress -Wunused-private-field
    (void)curses_;

    throw(goby::Exception("Tried to enable NCurses GUI without compiling against NCurses. Install "
                          "NCurses and recompile goby or disable GUI functionality"));
#endif
}

void goby::util::FlexOStreamBuf::add_group(const std::string& name, Group g)
{
    //    if(groups_.count(name)) return;

    groups_[name] = g;

#ifdef HAS_NCURSES
    if (is_gui_)
    {
        std::lock_guard<std::mutex> lock(curses_mutex);
        curses_->add_win(&groups_[name]);
    }
#endif
}

int goby::util::FlexOStreamBuf::overflow(int c /*= EOF*/)
{
    parent_->set_unset_verbosity();

    if (c == EOF)
        return c;
    else if (c == '\n')
        buffer_.push_back(std::string());
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
            << "== Misuse of goby::glog in threaded mode: must use 'goby.is(...) && glog' syntax"
            << std::endl;
        std::cerr << "== Offending line: " << buffer_.front() << std::endl;
        assert(!(lock_action_ == logger_lock::lock && current_verbosity_ == logger::UNKNOWN));
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
            basic_log_header(*cfg.os(), group_name_);
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
