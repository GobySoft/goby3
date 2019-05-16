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

#ifndef Time20100713H
#define Time20100713H

#include <ctime>
#include <sys/time.h>

#include <boost/asio/time_traits.hpp>
#include <boost/date_time.hpp>
#include <boost/function.hpp>

#include "goby/util/as.h"
#include "goby/util/primitive_types.h"
#include "time3.h"

/// All objects related to the Goby Underwater Autonomy Project
namespace goby
{
namespace common
{
inline double ptime2unix_double(boost::posix_time::ptime given_time)
{
    return time::from_ptime<time::SITime>(given_time).value();
}

inline boost::posix_time::ptime unix_double2ptime(double given_time)
{
    return time::to_ptime(time::SITime::from_value(given_time));
}

inline std::uint64_t ptime2unix_microsec(boost::posix_time::ptime given_time)
{
    return time::from_ptime<time::MicroTime>(given_time).value();
}

inline boost::posix_time::ptime unix_microsec2ptime(std::uint64_t given_time)
{
    return time::to_ptime(time::MicroTime::from_value(given_time));
}
} // namespace common

namespace util
{
template <typename To, typename From>
typename boost::enable_if<
    boost::mpl::and_<boost::is_same<To, double>, boost::is_same<From, boost::posix_time::ptime> >,
    To>::type
as(const From& from)
{
    return goby::common::ptime2unix_double(from);
}

template <typename To, typename From>
typename boost::enable_if<
    boost::mpl::and_<boost::is_same<To, boost::posix_time::ptime>, boost::is_same<From, double> >,
    To>::type
as(const From& from)
{
    return goby::common::unix_double2ptime(from);
}

template <typename To, typename From>
typename boost::enable_if<boost::mpl::and_<boost::is_same<To, std::uint64_t>,
                                           boost::is_same<From, boost::posix_time::ptime> >,
                          To>::type
as(const From& from)
{
    return goby::common::ptime2unix_microsec(from);
}

template <typename To, typename From>
typename boost::enable_if<boost::mpl::and_<boost::is_same<To, boost::posix_time::ptime>,
                                           boost::is_same<From, std::uint64_t> >,
                          To>::type
as(const From& from)
{
    return goby::common::unix_microsec2ptime(from);
}
} // namespace util

/// Utility objects for performing functions such as logging, non-acoustic communication (ethernet / serial), time, scientific, string manipulation, etc.
namespace common
{
template <typename ReturnType> ReturnType goby_time()
{
    static_assert(sizeof(ReturnType) == 0, "Invalid ReturnType for goby_time<>()");
}

extern boost::function0<std::uint64_t> goby_time_function;

template <> inline std::uint64_t goby_time<std::uint64_t>()
{
    if (!goby_time_function)
        return goby::time::now().value();
    else
        return goby_time_function();
}

template <> inline double goby_time<double>()
{
    return static_cast<double>(goby_time<std::uint64_t>()) / 1.0e6;
}

template <> inline boost::posix_time::ptime goby_time<boost::posix_time::ptime>()
{
    return util::as<boost::posix_time::ptime>(goby_time<std::uint64_t>());
}

inline boost::posix_time::ptime goby_time() { return goby_time<boost::posix_time::ptime>(); }

/// \brief Returns current UTC time as a human-readable string
template <> inline std::string goby_time<std::string>()
{
    return goby::util::as<std::string>(goby_time<boost::posix_time::ptime>());
}

/// Simple string representation of goby_time()
inline std::string goby_time_as_string(const boost::posix_time::ptime& t = goby_time())
{
    return goby::util::as<std::string>(t);
}

/// ISO string representation of goby_time()
inline std::string goby_file_timestamp()
{
    using namespace boost::posix_time;
    return to_iso_string(second_clock::universal_time());
}

/// convert to ptime from time_t from time.h (whole seconds since UNIX)
inline boost::posix_time::ptime time_t2ptime(std::time_t t)
{
    return boost::posix_time::from_time_t(t);
}

/// convert from ptime to time_t from time.h (whole seconds since UNIX)
inline std::time_t ptime2time_t(boost::posix_time::ptime t)
{
    std::tm out = boost::posix_time::to_tm(t);
    return mktime(&out);
}

/// time duration to double number of seconds: good to the microsecond
inline double time_duration2double(boost::posix_time::time_duration time_of_day)
{
    using namespace boost::posix_time;

    // prevent overflows in getting total seconds with call to ptime::total_seconds
    if (time_of_day.hours() > (0x7FFFFFFF / 3600))
        return std::numeric_limits<double>::infinity();
    else
        return (double(time_of_day.total_seconds()) +
                double(time_of_day.fractional_seconds()) /
                    double(time_duration::ticks_per_second()));
}

inline boost::posix_time::ptime nmea_time2ptime(const std::string& mt)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;

    std::string::size_type dot_pos = mt.find('.');

    // must be at least HHMMSS
    if (mt.length() < 6)
        return ptime(not_a_date_time);
    else
    {
        std::string s_fs = "0";
        // has some fractional seconds
        if (dot_pos != std::string::npos)
            s_fs = mt.substr(dot_pos + 1); // everything after the "."
        else
            dot_pos = mt.size();

        std::string s_hour = mt.substr(dot_pos - 6, 2), s_min = mt.substr(dot_pos - 4, 2),
                    s_sec = mt.substr(dot_pos - 2, 2);

        try
        {
            int hour = boost::lexical_cast<int>(s_hour);
            int min = boost::lexical_cast<int>(s_min);
            int sec = boost::lexical_cast<int>(s_sec);
            int micro_sec = boost::lexical_cast<int>(s_fs) * pow(10, 6 - s_fs.size());

            boost::gregorian::date return_date(boost::gregorian::day_clock::universal_day());
            boost::posix_time::time_duration return_duration(
                boost::posix_time::time_duration(hour, min, sec, 0) + microseconds(micro_sec));
            boost::posix_time::ptime return_time(return_date, return_duration);
            return return_time;
        }
        catch (boost::bad_lexical_cast&)
        {
            return ptime(not_a_date_time);
        }
    }
}

// dummy struct for use with boost::asio::time_traits
struct GobyTime
{
};

} // namespace common
} // namespace goby

namespace boost
{
namespace asio
{
/// Time traits specialised for GobyTime
template <> struct time_traits<goby::common::GobyTime>
{
    /// The time type.
    typedef boost::posix_time::ptime time_type;

    /// The duration type.
    typedef boost::posix_time::time_duration duration_type;

    /// Get the current time.
    static time_type now() { return goby::common::goby_time(); }

    /// Add a duration to a time.
    static time_type add(const time_type& t, const duration_type& d) { return t + d; }

    /// Subtract one time from another.
    static duration_type subtract(const time_type& t1, const time_type& t2) { return t1 - t2; }

    /// Test whether one time is less than another.
    static bool less_than(const time_type& t1, const time_type& t2) { return t1 < t2; }

    /// Convert to POSIX duration type.
    static boost::posix_time::time_duration to_posix_duration(const duration_type& d)
    {
        return d / goby::time::SimulatorSettings::warp_factor;
    }
};
} // namespace asio
} // namespace boost

#endif
