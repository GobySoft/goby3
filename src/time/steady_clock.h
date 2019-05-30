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

#ifndef TIME_STEADY_CLOCK_20190530H
#define TIME_STEADY_CLOCK_20190530H

#include <chrono>

#include "goby/time/simulation.h"

namespace goby
{
namespace time
{
struct SteadyClock
{
    typedef std::chrono::microseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<SteadyClock> time_point;
    static const bool is_steady = true;

    static time_point now() noexcept
    {
        using namespace std::chrono;
        auto now = steady_clock::now();

        if (!SimulatorSettings::using_sim_time)
            return time_point(duration_cast<duration>(now.time_since_epoch()));
        else
            return time_point(SimulatorSettings::warp_factor *
                              duration_cast<duration>(now.time_since_epoch()));
    }
};
} // namespace time
} // namespace goby

#endif
