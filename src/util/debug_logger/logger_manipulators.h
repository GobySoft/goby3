// Copyright 2012-2020:
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

#ifndef GOBY_UTIL_DEBUG_LOGGER_LOGGER_MANIPULATORS_H
#define GOBY_UTIL_DEBUG_LOGGER_LOGGER_MANIPULATORS_H

#include <iostream>
#include <string>

#include "term_color.h"

namespace goby
{
namespace util
{
class FlexOstream;

namespace logger
{
/// label stream as "error"
inline std::ostream& die(std::ostream& os)
{
    return (os << goby::util::tcolor::red << "(Error): " << goby::util::tcolor::nocolor);
}

/// label stream as "warning"
inline std::ostream& warn(std::ostream& os)
{
    return (os << goby::util::tcolor::red << "(Warning): " << goby::util::tcolor::nocolor);
}

/// label stream as "verbose"
inline std::ostream& verbose(std::ostream& os) { return (os); }

/// label stream as "debug1"
inline std::ostream& debug1(std::ostream& os) { return (os << "D: "); }

/// label stream as "debug2"
inline std::ostream& debug2(std::ostream& os) { return (os << "D2: "); }

/// label stream as "debug3"
inline std::ostream& debug3(std::ostream& os) { return (os << "D3: "); }


/// Defines a group of messages to be sent to the Goby logger. For Verbosity == verbose streams, all entries appear interleaved, but each group is offset with a different color. For Verbosity == gui streams, all groups have a separate subwindow.
class Group
{
  public:
    Group(const std::string& name = "", const std::string& description = "",
          goby::util::Colors::Color color = goby::util::Colors::nocolor)
        : name_(name), description_(description), color_(color), enabled_(true)
    {
    }

    /// \name Getters
    //@{
    /// Name of this group (used in the group manipulator)
    std::string name() const { return name_; }
    /// Human readable description of this group
    std::string description() const { return description_; }
    /// Color to use when displaying this group (for streams that support terminal escape codes only: std::cout, std::cerr, std::clog)
    goby::util::Colors::Color color() const { return color_; }
    /// Is this group enabled?
    bool enabled() const { return enabled_; }
    //@}

    /// \name Setters
    //@{
    void name(const std::string& s) { name_ = s; }
    void description(const std::string& s) { description_ = s; }
    void color(goby::util::Colors::Color c) { color_ = c; }
    void enabled(bool b) { enabled_ = b; }
    //@}

  private:
    std::string name_;
    std::string description_;
    goby::util::Colors::Color color_;
    bool enabled_;
};

/// Helper class for enabling the group(std::string) manipulator
class GroupSetter
{
  public:
    explicit GroupSetter(const std::string& s) : group_(s) {}
    void operator()(std::ostream& os) const;
    void operator()(goby::util::FlexOstream& os) const;

  private:
    std::string group_;
};

/// used for non tty ostreams (everything but std::cout / std::cerr) as the header for every line
std::ostream& basic_log_header(std::ostream& os, const std::string& group_name);

std::ostream& operator<<(std::ostream& os, const Group& g);
inline std::ostream& operator<<(std::ostream& os, const GroupSetter& gs)
{
    gs(os);
    return (os);
}

goby::util::FlexOstream& operator<<(goby::util::FlexOstream& os, const GroupSetter& gs);

} // namespace logger
} // namespace util
} // namespace goby

using goby::util::logger::operator<<;

inline goby::util::logger::GroupSetter group(std::string n)
{
    return (goby::util::logger::GroupSetter(n));
}

#endif
