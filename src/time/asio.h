// Copyright 2020:
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

#ifndef GOBY_TIME_ASIO_H
#define GOBY_TIME_ASIO_H

#include <boost/asio/time_traits.hpp>
#include <boost/date_time.hpp>

namespace goby
{
namespace time
{
// dummy struct for use with boost::asio::time_traits
struct ASIOGobyTime
{
};

} // namespace time
} // namespace goby

namespace boost
{
namespace asio
{
/// Time traits specialised for GobyTime
template <> struct time_traits<goby::time::ASIOGobyTime>
{
    /// The time type.
    typedef boost::posix_time::ptime time_type;

    /// The duration type.
    typedef boost::posix_time::time_duration duration_type;

    /// Get the current time.
    static time_type now() { return goby::time::SystemClock::now<boost::posix_time::ptime>(); }

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
