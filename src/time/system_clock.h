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

#ifndef TIME_SYSTEM_CLOCK_20190530H
#define TIME_SYSTEM_CLOCK_20190530H

#include <chrono>
#include <cstdint>

#include "goby/time/simulation.h"

namespace goby
{
///\name Time
//@{

/// Functions and objects related to timekeeping.
namespace time
{
struct SystemClock
{
    // use microseconds to avoid overflow at higher warp values
    typedef std::chrono::microseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<SystemClock> time_point;
    static const bool is_steady = false;

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
