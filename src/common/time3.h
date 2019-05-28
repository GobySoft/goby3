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

#ifndef Time3_20180322H
#define Time3_20180322H

#include <chrono>
#include <limits>
#include <sstream>
#include <type_traits>

#include <boost/date_time.hpp>

#include <boost/units/cmath.hpp>
#include <boost/units/quantity.hpp>
#include <boost/units/systems/si/prefixes.hpp>
#include <boost/units/systems/si/time.hpp>

namespace goby
{
///\name Time
//@{

/// Functions and objects related to timekeeping.
namespace time
{
/// \brief microsecond unit
using MicroTimeUnit = decltype(boost::units::si::micro* boost::units::si::seconds);
/// \brief quantity of microseconds (using int64)
using MicroTime = boost::units::quantity<MicroTimeUnit, std::int64_t>;
/// \brief quantity of seconds (using double)
using SITime = boost::units::quantity<boost::units::si::time, double>;

struct SimulatorSettings
{
    static bool using_sim_time;
    static int warp_factor;
    static std::chrono::system_clock::time_point reference_time;
};

struct Clock
{
    typedef std::chrono::microseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<Clock> time_point;
    static const bool is_steady = false;

    static time_point now() noexcept
    {
        using namespace std::chrono;
        auto now = system_clock::now();

        // warp time (t) by warp factor (w), relative to reference_time (t0)
        // so t_sim = (t-t0)*w+t0
        if (SimulatorSettings::using_sim_time)
        {
            auto duration_since_reference = now - SimulatorSettings::reference_time;
            auto warped_duration_since_reference =
                SimulatorSettings::warp_factor * duration_since_reference;
            return time_point(
                duration_cast<duration>(warped_duration_since_reference) +
                duration_cast<duration>(SimulatorSettings::reference_time.time_since_epoch()));
        }
        else
        {
            return time_point(duration_cast<duration>(now.time_since_epoch()));
        }
    }
};

/// \brief Convert between time representations (this function works for tautological conversions and for conversions between boost::units types where explicit construction does the conversion)
template <typename ToTimeType, typename FromTimeType,
          typename std::enable_if<std::is_same<ToTimeType, FromTimeType>{}, int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    return from_time;
};

template <
    typename ToTimeType, typename FromTimeType,
    typename ToUnitType = typename ToTimeType::unit_type,
    typename ToValueType = typename ToTimeType::value_type,
    typename FromUnitType = typename FromTimeType::unit_type,
    typename FromValueType = typename FromTimeType::value_type,
    typename std::enable_if<
        !std::is_same<ToTimeType, FromTimeType>{} &&
            std::is_same<ToTimeType, boost::units::quantity<ToUnitType, ToValueType> >{} &&
            std::is_same<FromTimeType, boost::units::quantity<FromUnitType, FromValueType> >{},
        int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    return ToTimeType(from_time);
};

template <typename ToTimeType, typename FromTimeType,
          typename std::enable_if<
              !std::is_same<ToTimeType, FromTimeType>{} &&
                  (std::is_same<FromTimeType, Clock::time_point>{} ||
                   std::is_same<FromTimeType, std::chrono::system_clock::time_point>{}),
              int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    std::int64_t microsecs_since_epoch =
        from_time.time_since_epoch() / std::chrono::microseconds(1);
    {
        using namespace boost::units::si;
        return ToTimeType(microsecs_since_epoch * micro * seconds);
    }
}

template <
    typename ToTimeType, typename FromTimeType,
    typename std::enable_if<!std::is_same<ToTimeType, FromTimeType>{} &&
                                (std::is_same<ToTimeType, Clock::time_point>{} ||
                                 std::is_same<ToTimeType, std::chrono::system_clock::time_point>{}),
                            int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    std::int64_t microsecs_since_epoch = MicroTime(from_time).value();
    auto duration_since_epoch = std::chrono::microseconds(microsecs_since_epoch);
    return ToTimeType(
        std::chrono::duration_cast<typename ToTimeType::duration>(duration_since_epoch));
}

/// \brief return the current system clock time since 1970-01-01 00:00 UTC ("UNIX Time")
template <typename TimeType = MicroTime> inline TimeType now()
{
    return convert<TimeType, Clock::time_point>(Clock::now());
}

/// \brief Convert from boost::posix_time::ptime to boost::units::quantity<...> of time
template <typename TimeType = MicroTime> TimeType from_ptime(boost::posix_time::ptime time_in)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;

    if (time_in == not_a_date_time)
    {
        return convert<TimeType, MicroTime>(MicroTime::from_value(-1));
    }
    else
    {
        const int MICROSEC_IN_SEC = 1000000;

        date_duration date_diff = time_in.date() - date(1970, 1, 1);
        time_duration time_diff = time_in.time_of_day();

        return convert<TimeType, MicroTime>(MicroTime::from_value(
            static_cast<std::int64_t>(date_diff.days()) * 24 * 3600 * MICROSEC_IN_SEC +
            static_cast<std::int64_t>(time_diff.total_seconds()) * MICROSEC_IN_SEC +
            static_cast<std::int64_t>(time_diff.fractional_seconds()) /
                (time_duration::ticks_per_second() / MICROSEC_IN_SEC)));
    }
}

/// \brief Convert from boost::posix_time::ptime to boost::units::quantity<...> of time
template <typename TimeType> boost::posix_time::ptime to_ptime(TimeType time_in)
{
    std::int64_t time_in_value = convert<MicroTime, TimeType>(time_in) / MicroTimeUnit();

    using namespace boost::posix_time;
    using namespace boost::gregorian;

    if (time_in_value == -1)
        return boost::posix_time::ptime(not_a_date_time);
    else
    {
        const int MICROSEC_IN_SEC = 1000000;
        ptime time_t_epoch(date(1970, 1, 1));
        std::int64_t m = time_in_value / MICROSEC_IN_SEC / 60;
        std::int64_t s = (time_in_value / MICROSEC_IN_SEC) - m * 60;
        std::int64_t micro_s = (time_in_value - (s + m * 60) * MICROSEC_IN_SEC);
        return time_t_epoch + minutes(m) + seconds(s) + microseconds(micro_s);
    }
}

/// \brief Returns current UTC date-time as a human-readable string
template <typename TimeType = MicroTime> inline std::string str(TimeType value = now<TimeType>())
{
    std::stringstream ss;
    ss << to_ptime(value);
    return ss.str();
}

/// \brief Returns current UTC date-time as an ISO string suitable for file names (no spaces or special characters, e.g. 20180322T215258)
template <typename TimeType = MicroTime>
inline std::string file_str(TimeType value = now<TimeType>())
{
    auto rounded_seconds = boost::units::round(convert<SITime, TimeType>(value));
    return boost::posix_time::to_iso_string(to_ptime(rounded_seconds));
}
} // namespace time

//@}

} // namespace goby

#endif
