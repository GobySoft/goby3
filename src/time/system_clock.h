// Copyright 2019-2020:
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

#ifndef TIME_SYSTEM_CLOCK_20190530H
#define TIME_SYSTEM_CLOCK_20190530H

#include <chrono>
#include <cstdint>

#include "goby/time/simulation.h"

namespace goby
{
///\name Time
//@{

/// \brief Functions and objects related to timekeeping.
///
/// Goby timekeeping revolves around C++ chrono concepts, such as SystemClock (absolute world-referenced time) and SteadyClock (non-synchronized time that monotonically increases). These are thin wrappers are the std::chrono equivalents for the primary purpose of supporting simulation time that can proceed at a multiplier of real time.
///
/// For maximizing capability with other projects, Goby supports conversion (using the convert family of functions) amongst SystemClock and two other time representations: boost::units::quantity of time (e.g. boost::units::quantity<boost::units::si::time>) and boost::posix_time::ptime. DCCL uses the boost::units representation, and boost::posix_time::ptime was used by numerous projects prior to the release of std::chrono (and is still necessary until C++20 for its date functionality).
///
/// SystemClock (and std::chrono) distinguish between a time_point (an absolute point in time, e.g. July 31, 2019 at 11:45:32) and a time duration (e.g. 3 hours and 23 minutes). This distinction is not supported when using boost::units::quantity of time, so when calling the convert_duration functions, a boost::units::quantity of time represents a duration, whereas when calling the convert functions, a boost::units::quantity of time represents the number of seconds since the UNIX epoch (1970 Jan 1 at 00:00:00 UTC).
namespace time
{
/// \brief Essentially the same as std::chrono::system_clock except the time returned by SystemClock::now() can be "warped" (made to run faster than real time) for simulation purposes. To do this, set the appropriate parameters in SimulatorSettings.
struct SystemClock
{
    /// \brief Duration type
    ///
    /// We use microseconds (not nanoseconds) to avoid overflow at higher warp values
    typedef std::chrono::microseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<SystemClock> time_point;
    static const bool is_steady = false;

    /// \brief Returns the current system time unless SimulatorSettings::using_sim_time is set to true, in which case a simulated time is returned that is sped up by the SimulatorSettings::warp_factor
    ///
    /// When using simulated time, the returned time (t_sim) is computed relative to SimulatorSettings::reference_time (t_0) with an accelerated progression by a factor of the SimulatorSettings::warp_time (w) such that:
    /// t_sim = (t-t_0)*w + t_0
    /// A note when using MOOS middleware's MOOSTimeWarp: the value returned by this function is the same as MOOSTime() when \code SimulatorSettings::reference_time == 0 \endcode
    static time_point now() noexcept
    {
        using namespace std::chrono;
        auto now = system_clock::now();

        if (!SimulatorSettings::using_sim_time)
        {
            return time_point(duration_cast<duration>(now.time_since_epoch()));
        }
        else
        {
            // warp time (t) by warp factor (w), relative to reference_time (t0)
            // so t_sim = (t-t0)*w+t0
            std::int64_t microseconds_since_reference =
                (now - SimulatorSettings::reference_time) / microseconds(1);
            std::int64_t warped_microseconds_since_reference =
                SimulatorSettings::warp_factor * microseconds_since_reference;
            return time_point(
                duration_cast<duration>(microseconds(warped_microseconds_since_reference)) +
                duration_cast<duration>(SimulatorSettings::reference_time.time_since_epoch()));
        }
    }

    /// \brief return the current system clock time in one of the representations supported by the convert() family of functions
    template <typename TimeType> static TimeType now();
};

} // namespace time

//@}

} // namespace goby

#endif
