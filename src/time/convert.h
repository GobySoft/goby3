// Copyright 2018-2020:
//   GobySoft, LLC (2013-)
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

#ifndef GOBY_TIME_CONVERT_H
#define GOBY_TIME_CONVERT_H

#include <chrono>
#include <sstream>
#include <type_traits>

#include <boost/date_time.hpp>
#include <boost/units/cmath.hpp>

#include "goby/time/system_clock.h"
#include "goby/time/types.h"

namespace goby
{
namespace time
{
/// \brief Convert between time representations (this function works for tautological conversions)
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \tparam FromTimeType The type of the input time
/// \param from_time Time to convert
/// \return Converted time
template <typename ToTimeType, typename FromTimeType,
          typename std::enable_if<std::is_same<ToTimeType, FromTimeType>{}, int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    return from_time;
};

/// \brief Convert between time representations (this function converts between two different boost::units::quantity of time)
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \tparam FromTimeType The type of the input time
/// \param from_time Time to convert
/// \return Converted time
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

/// \brief Convert between time representations (this function converts to a boost::units::quantity of time from either a chrono::system_clock::time_point or a goby::time::SystemClock::time_point)
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \tparam FromTimeType The type of the input time
/// \param from_time Time to convert
/// \return Converted time
template <typename ToTimeType, typename FromTimeType,
          typename ToUnitType = typename ToTimeType::unit_type,
          typename ToValueType = typename ToTimeType::value_type,
          typename std::enable_if<
              std::is_same<ToTimeType, boost::units::quantity<ToUnitType, ToValueType> >{} &&
                  (std::is_same<FromTimeType, SystemClock::time_point>{} ||
                   std::is_same<FromTimeType, std::chrono::system_clock::time_point>{}),
              int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    std::int64_t microsecs_since_epoch =
        from_time.time_since_epoch() / std::chrono::microseconds(1);
    {
        return ToTimeType(microsecs_since_epoch * boost::units::si::micro *
                          boost::units::si::seconds);
    }
}

/// \brief Convert between time representations (this function converts to either a chrono::system_clock::time_point or a goby::time::SystemClock::time_point from a boost::units::quantity of time)
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \tparam FromTimeType The type of the input time
/// \param from_time Time to convert
/// \return Converted time
template <
    typename ToTimeType, typename FromTimeType,
    typename FromUnitType = typename FromTimeType::unit_type,
    typename FromValueType = typename FromTimeType::value_type,
    typename std::enable_if<
        (std::is_same<ToTimeType, SystemClock::time_point>{} ||
         std::is_same<ToTimeType, std::chrono::system_clock::time_point>{}) &&
            std::is_same<FromTimeType, boost::units::quantity<FromUnitType, FromValueType> >{},
        int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    std::int64_t microsecs_since_epoch = MicroTime(from_time).value();
    auto duration_since_epoch = std::chrono::microseconds(microsecs_since_epoch);
    return ToTimeType(
        std::chrono::duration_cast<typename ToTimeType::duration>(duration_since_epoch));
}

/// \brief Convert between time representations (this function converts from a time type supported by the other convert functions to a boost::posix_time::ptime)
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \tparam FromTimeType The type of the input time
/// \param from_time Time to convert
/// \return Converted time
template <typename ToTimeType, typename FromTimeType,
          typename std::enable_if<!std::is_same<ToTimeType, FromTimeType>{} &&
                                      std::is_same<ToTimeType, boost::posix_time::ptime>{},
                                  int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    std::int64_t time_in_value = convert<MicroTime, FromTimeType>(from_time) / MicroTimeUnit();

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

/// \brief Convert between time representations (this function converts from a boost::posix_time::ptime to a time type supported by the other convert functions)
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \tparam FromTimeType The type of the input time
/// \param from_time Time to convert
/// \return Converted time
template <typename ToTimeType, typename FromTimeType,
          typename std::enable_if<!std::is_same<ToTimeType, FromTimeType>{} &&
                                      std::is_same<FromTimeType, boost::posix_time::ptime>{},
                                  int>::type = 0>
ToTimeType convert(FromTimeType from_time)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;

    if (from_time == not_a_date_time)
    {
        return convert<ToTimeType, MicroTime>(MicroTime::from_value(-1));
    }
    else
    {
        const int MICROSEC_IN_SEC = 1000000;

        date_duration date_diff = from_time.date() - date(1970, 1, 1);
        time_duration time_diff = from_time.time_of_day();

        return convert<ToTimeType, MicroTime>(MicroTime::from_value(
            static_cast<std::int64_t>(date_diff.days()) * 24 * 3600 * MICROSEC_IN_SEC +
            static_cast<std::int64_t>(time_diff.total_seconds()) * MICROSEC_IN_SEC +
            static_cast<std::int64_t>(time_diff.fractional_seconds()) /
                (time_duration::ticks_per_second() / MICROSEC_IN_SEC)));
    }
}

/// \brief Returns the provided time (or current time if omitted) as a human-readable string
///
/// \tparam TimeType The type of the input time
/// \param value Time to convert
/// \return Time formatted as a human readable string (e.g. for debug logging)
template <typename TimeType = SystemClock::time_point>
inline std::string str(TimeType value = SystemClock::now<TimeType>())
{
    std::stringstream ss;
    ss << convert<boost::posix_time::ptime>(value);
    return ss.str();
}

/// \brief Returns the provided time (or current time if omitted) as an ISO string suitable for file names (no spaces or special characters, e.g. 20180322T215258)
///
/// \tparam TimeType The type of the input time
/// \param value Time to convert
/// \return Time formatted in a way suitable for file names
template <typename TimeType = SystemClock::time_point>
inline std::string file_str(TimeType value = SystemClock::now<TimeType>())
{
    auto rounded_seconds = boost::units::round(convert<SITime, TimeType>(value));
    return boost::posix_time::to_iso_string(convert<boost::posix_time::ptime>(rounded_seconds));
}

/// \brief Convert between duration representations (this function works for tautological conversions)
///
/// \tparam ToDurationType The type of the returned (converted) duration
/// \tparam FromDurationType The type of the input duration
/// \param from_duration Duration to convert
/// \return Converted duration
template <typename ToDurationType, typename FromDurationType,
          typename std::enable_if<std::is_same<ToDurationType, FromDurationType>{}, int>::type = 0>
ToDurationType convert_duration(FromDurationType from_duration)
{
    return from_duration;
};

/// \brief Convert between duration representations (this function converts between two different boost::units::quantity of time)
///
/// \tparam ToDurationType The type of the returned (converted) duration
/// \tparam FromDurationType The type of the input duration
/// \param from_duration Duration to convert
/// \return Converted duration
template <
    typename ToDurationType, typename FromDurationType,
    typename ToUnitType = typename ToDurationType::unit_type,
    typename ToValueType = typename ToDurationType::value_type,
    typename FromUnitType = typename FromDurationType::unit_type,
    typename FromValueType = typename FromDurationType::value_type,
    typename std::enable_if<
        !std::is_same<ToDurationType, FromDurationType>{} &&
            std::is_same<ToDurationType, boost::units::quantity<ToUnitType, ToValueType> >{} &&
            std::is_same<FromDurationType, boost::units::quantity<FromUnitType, FromValueType> >{},
        int>::type = 0>
ToDurationType convert_duration(FromDurationType from_duration)
{
    return ToDurationType(from_duration);
};

/// \brief Convert between duration representations (this function converts from std::chrono::duration to boost::units::quantity of time)
///
/// \tparam ToDurationType The type of the returned (converted) duration
/// \tparam FromDurationType The type of the input duration
/// \param from_duration Duration to convert
/// \return Converted duration
template <
    typename ToDurationType, typename FromDurationType,
    typename ToUnitType = typename ToDurationType::unit_type,
    typename ToValueType = typename ToDurationType::value_type,
    typename FromRepType = typename FromDurationType::rep,
    typename FromPeriodType = typename FromDurationType::period,
    typename std::enable_if<
        !std::is_same<ToDurationType, FromDurationType>{} &&
            std::is_same<ToDurationType, boost::units::quantity<ToUnitType, ToValueType> >{} &&
            std::is_same<FromDurationType, std::chrono::duration<FromRepType, FromPeriodType> >{},
        int>::type = 0>
ToDurationType convert_duration(FromDurationType from_duration)
{
    return ToDurationType(MicroTime::from_value(
        std::chrono::duration_cast<std::chrono::microseconds>(from_duration).count()));
};

/// \brief Convert between duration representations (this function converts from boost::units::quantity of time to std::chrono::duration
///
/// \tparam ToDurationType The type of the returned (converted) duration
/// \tparam FromDurationType The type of the input duration
/// \param from_duration Duration to convert
/// \return Converted duration
template <
    typename ToDurationType, typename FromDurationType,
    typename ToRepType = typename ToDurationType::rep,
    typename ToPeriodType = typename ToDurationType::period,
    typename FromUnitType = typename FromDurationType::unit_type,
    typename FromValueType = typename FromDurationType::value_type,
    typename std::enable_if<
        !std::is_same<ToDurationType, FromDurationType>{} &&
            std::is_same<ToDurationType, std::chrono::duration<ToRepType, ToPeriodType> >{} &&
            std::is_same<FromDurationType, boost::units::quantity<FromUnitType, FromValueType> >{},
        int>::type = 0>
ToDurationType convert_duration(FromDurationType from_duration)
{
    MicroTime microtime_duration(from_duration);
    return std::chrono::duration_cast<ToDurationType>(
        std::chrono::microseconds(microtime_duration.value()));
};

/// \brief Convert from NMEA0183 time representations (i.e. "HHMMSS[.SSSS]") to any time format supported by the goby::time::convert function family. This function assumes the time is from the current day.
///
/// \tparam ToTimeType The type of the returned (converted) time
/// \param mt NMEA0183 time of day as string (e.g. "124511" or "12.4511.1234" with variable fractional seconds up and including microsecond precision
/// \param return_date Date assigned to the input time when returned as a time point (defaults to the current day in UTC)
/// \return Converted time
template <typename ToTimeType>
ToTimeType
convert_from_nmea(const std::string& mt,
                  boost::gregorian::date return_date = boost::gregorian::day_clock::universal_day())
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;

    std::string::size_type dot_pos = mt.find('.');

    // must be at least HHMMSS
    if (mt.length() < 6)
        return convert<ToTimeType>(ptime(not_a_date_time));
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

            boost::posix_time::time_duration return_duration(
                boost::posix_time::time_duration(hour, min, sec, 0) + microseconds(micro_sec));
            boost::posix_time::ptime return_time(return_date, return_duration);
            return convert<ToTimeType>(return_time);
        }
        catch (boost::bad_lexical_cast&)
        {
            return convert<ToTimeType>(ptime(not_a_date_time));
        }
    }
}

} // namespace time
} // namespace goby

template <typename TimeType> TimeType goby::time::SystemClock::now()
{
    return goby::time::convert<TimeType, goby::time::SystemClock::time_point>(now());
}

#endif
