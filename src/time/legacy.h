// Copyright 2010-2020:
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

#ifndef GOBY_TIME_LEGACY_H
#define GOBY_TIME_LEGACY_H

#include <ctime>
#include <sys/time.h>


#include "goby/time/convert.h"
#include "goby/time/system_clock.h"
#include "goby/util/as.h"
#include "goby/util/primitive_types.h"

/// All objects related to the Goby Underwater Autonomy Project
namespace goby
{
/// Deprecated functions largely related to time handling (use goby::time for new work)
namespace common
{
[[deprecated("use time::convert()")]] inline double
ptime2unix_double(boost::posix_time::ptime given_time)
{
    return time::convert<time::SITime>(given_time).value();
}

[[deprecated("use time::convert()")]] inline boost::posix_time::ptime
unix_double2ptime(double given_time)
{
    return time::convert<boost::posix_time::ptime>(time::SITime::from_value(given_time));
}

[[deprecated("use time::convert()")]] inline std::uint64_t
ptime2unix_microsec(boost::posix_time::ptime given_time)
{
    return time::convert<time::MicroTime>(given_time).value();
}

[[deprecated("use time::convert()")]] inline boost::posix_time::ptime
unix_microsec2ptime(std::uint64_t given_time)
{
    return time::convert<boost::posix_time::ptime>(time::MicroTime::from_value(given_time));
}
} // namespace common

namespace util
{
template <typename To, typename From>
[[deprecated("use time::convert()")]] typename boost::enable_if<
    boost::mpl::and_<boost::is_same<To, double>, boost::is_same<From, boost::posix_time::ptime> >,
    To>::type
as(const From& from)
{
    return goby::common::ptime2unix_double(from);
}

template <typename To, typename From>
[[deprecated("use time::convert()")]] typename boost::enable_if<
    boost::mpl::and_<boost::is_same<To, boost::posix_time::ptime>, boost::is_same<From, double> >,
    To>::type
as(const From& from)
{
    return goby::common::unix_double2ptime(from);
}

template <typename To, typename From>
[[deprecated("use time::convert()")]]
    typename boost::enable_if<boost::mpl::and_<boost::is_same<To, std::uint64_t>,
                                               boost::is_same<From, boost::posix_time::ptime> >,
                              To>::type
    as(const From& from)
{
    return goby::common::ptime2unix_microsec(from);
}

template <typename To, typename From>
[[deprecated("use time::convert()")]]
    typename boost::enable_if<boost::mpl::and_<boost::is_same<To, boost::posix_time::ptime>,
                                               boost::is_same<From, std::uint64_t> >,
                              To>::type
    as(const From& from)
{
    return goby::common::unix_microsec2ptime(from);
}
} // namespace util

namespace common
{
template <typename ReturnType>
[[deprecated("use goby::time::SystemClock::now()")]] ReturnType goby_time() {
    static_assert(sizeof(ReturnType) == 0, "Invalid ReturnType for goby_time<>()");
}

template <>
[[deprecated("use goby::time::SystemClock::now<goby::time::MicroTime>().value()")]] inline std::
    uint64_t goby_time<std::uint64_t>()
{
    return goby::time::SystemClock::now<time::MicroTime>().value();
}

template <>
[[deprecated("use goby::time::SystemClock::now<goby::time::SITime>().value()")]] inline double
goby_time<double>()
{
    return static_cast<double>(goby_time<std::uint64_t>()) / 1.0e6;
}

template <>
[[deprecated("use goby::time::SystemClock::now<boost::posix_time::ptime>()")]] inline boost::
    posix_time::ptime
    goby_time<boost::posix_time::ptime>()
{
    return util::as<boost::posix_time::ptime>(goby_time<std::uint64_t>());
}

[[deprecated("use goby::time::SystemClock::now<boost::posix_time::ptime>()")]] inline boost::
    posix_time::ptime
    goby_time()
{
    return goby_time<boost::posix_time::ptime>();
}

/// \brief Returns current UTC time as a human-readable string
template <>[[deprecated("use goby::time::str()")]] inline std::string goby_time<std::string>()
{
    return goby::util::as<std::string>(goby_time<boost::posix_time::ptime>());
}

/// Simple string representation of goby_time()
[[deprecated("use goby::time::str()")]] inline std::string
goby_time_as_string(const boost::posix_time::ptime& t = goby_time())
{
    return goby::util::as<std::string>(t);
}

/// ISO string representation of goby_time()
[[deprecated("use goby::time::file_str()")]] inline std::string goby_file_timestamp()
{
    using namespace boost::posix_time;
    return to_iso_string(second_clock::universal_time());
}

/// convert to ptime from time_t from time.h (whole seconds since UNIX)
[[deprecated]] inline boost::posix_time::ptime time_t2ptime(std::time_t t)
{
    return boost::posix_time::from_time_t(t);
}

/// convert from ptime to time_t from time.h (whole seconds since UNIX)
[[deprecated]] inline std::time_t ptime2time_t(boost::posix_time::ptime t)
{
    std::tm out = boost::posix_time::to_tm(t);
    return mktime(&out);
}

[[deprecated("use convert_from_nmea")]] inline boost::posix_time::ptime
nmea_time2ptime(const std::string& mt)
{
    return time::convert_from_nmea<boost::posix_time::ptime>(mt);
}

} // namespace common
} // namespace goby

#endif
