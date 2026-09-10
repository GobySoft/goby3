// Copyright 2026:
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_DETAIL_SIMULATION_TIME_H
#define GOBY_MIDDLEWARE_APPLICATION_DETAIL_SIMULATION_TIME_H

#include <chrono> // for microseconds, system_clock

#include "goby/middleware/protobuf/app_config.pb.h" // for AppConfig
#include "goby/time/simulation.h"                   // for SimulatorSettings

namespace goby
{
namespace middleware
{
namespace detail
{
/// \brief Applies the simulation time settings from an application configuration to goby::time::SimulatorSettings
///
/// This must be called before the Application is constructed. It is used by goby::run as well as
/// by the language binding wrappers (e.g. Julia, Python), which set the application configuration
/// up themselves rather than going through goby::run.
///
/// \param app_cfg The base application configuration (goby::middleware::protobuf::AppConfig)
inline void configure_simulation_time(const protobuf::AppConfig& app_cfg)
{
    if (!app_cfg.simulation().time().use_sim_time())
        return;

    goby::time::SimulatorSettings::using_sim_time = true;
    goby::time::SimulatorSettings::warp_factor = app_cfg.simulation().time().warp_factor();

    if (app_cfg.simulation().time().has_reference_microtime())
        goby::time::SimulatorSettings::reference_time = std::chrono::system_clock::time_point(
            std::chrono::microseconds(app_cfg.simulation().time().reference_microtime()));
}

} // namespace detail
} // namespace middleware
} // namespace goby

#endif
