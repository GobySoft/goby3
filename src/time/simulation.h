// Copyright 2019:
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

#ifndef TIME_SIMULATION_20190530H
#define TIME_SIMULATION_20190530H

#include <chrono>

namespace goby
{
namespace time
{
/// \brief Parameters for enabling and configuring simulation time
struct SimulatorSettings
{
    /// \brief Enables simulation time if true (if false, none of the remaining parameters are used)
    static bool using_sim_time;
    /// \brief Warp factor to speed up (or slow time) the time values returned by SteadyClock::now() and SystemClock::now(). For example, to double the speed of the clocks, set this value to 2.
    static int warp_factor;
    /// \brief Reference time when calculating SystemClock::now(). If this is unset, the default is 1 January of the current year.
    static std::chrono::system_clock::time_point reference_time;
};

} // namespace time
} // namespace goby

#endif
