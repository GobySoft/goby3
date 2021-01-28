// Copyright 2013-2020:
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

#ifndef GOBY_UTIL_SALINITY_H
#define GOBY_UTIL_SALINITY_H

#include <cmath>

#include <boost/units/quantity.hpp>
#include <boost/units/systems/si.hpp>
#include <boost/units/systems/si/prefixes.hpp>
#include <boost/units/systems/temperature/celsius.hpp>

#include "goby/util/seawater/detail/salinity_impl.h"
#include "units.h"

namespace goby
{
namespace util
{
namespace seawater
{
static const boost::units::quantity<decltype(milli_siemens_per_cm)> conductivity_at_standard{
    42.914 * milli_siemens_per_cm}; // S = 35, T = 15 deg C, P = 0 dbar

/// \brief Calculates salinity from conductivity, temperature, and pressure
/// Adapted from "Algorithms for computation of fundamental properties of seawater; UNESCO technical papers in marine science; Vol.:44; 1983"
/// https://unesdoc.unesco.org/ark:/48223/pf0000059832
/// \param conductivity Conductivity
/// \param temperature Temperature
/// \param pressure Pressure
/// \return computed salinity
template <typename ConductivityUnit = decltype(milli_siemens_per_cm),
          typename TemperatureUnit = boost::units::celsius::temperature,
          typename PressureUnit = decltype(boost::units::si::deci* bar)>
boost::units::quantity<boost::units::si::dimensionless>
salinity(boost::units::quantity<ConductivityUnit> conductivity,
         boost::units::quantity<boost::units::absolute<TemperatureUnit> > temperature,
         boost::units::quantity<PressureUnit> pressure)
{
    using namespace boost::units;
    double CND = quantity<decltype(milli_siemens_per_cm)>(conductivity) / conductivity_at_standard;
    double T = quantity<absolute<celsius::temperature> >(temperature).value();
    double P = quantity<decltype(si::deci * bar)>(pressure).value();

    return quantity<si::dimensionless>(
        detail::SalinityCalculator::compute(CND, T, P, detail::SalinityCalculator::TO_SALINITY));
}

/// \brief Calculates conductivity from salinity, temperature, and pressure
/// Adapted from "Algorithms for computation of fundamental properties of seawater; UNESCO technical papers in marine science; Vol.:44; 1983"
/// https://unesdoc.unesco.org/ark:/48223/pf0000059832
/// \param salinity Salinity
/// \param temperature Temperature
/// \param pressure Pressure
/// \return computed conductivity
template <typename TemperatureUnit = boost::units::celsius::temperature,
          typename DimensionlessUnit = boost::units::si::dimensionless,
          typename PressureUnit = decltype(boost::units::si::deci* bar)>
boost::units::quantity<decltype(milli_siemens_per_cm)>
conductivity(boost::units::quantity<DimensionlessUnit> salinity,
             boost::units::quantity<boost::units::absolute<TemperatureUnit> > temperature,
             boost::units::quantity<PressureUnit> pressure)
{
    using namespace boost::units;
    double SAL = salinity;
    double T = quantity<absolute<celsius::temperature> >(temperature).value();
    double P = quantity<decltype(si::deci * bar)>(pressure).value();

    return detail::SalinityCalculator::compute(SAL, T, P,
                                               detail::SalinityCalculator::FROM_SALINITY) *
           conductivity_at_standard;
}

/// \brief Calculates conductivity from salinity, temperature, and pressure (variant that accepts plain double for salinity)
/// Adapted from "Algorithms for computation of fundamental properties of seawater; UNESCO technical papers in marine science; Vol.:44; 1983"
/// https://unesdoc.unesco.org/ark:/48223/pf0000059832
/// \param salinity Salinity
/// \param temperature Temperature
/// \param pressure Pressure
/// \return computed conductivity
template <typename TemperatureUnit = boost::units::celsius::temperature,
          typename PressureUnit = decltype(boost::units::si::deci* bar)>
boost::units::quantity<decltype(milli_siemens_per_cm)>
conductivity(double salinity,
             boost::units::quantity<boost::units::absolute<TemperatureUnit> > temperature,
             boost::units::quantity<PressureUnit> pressure)
{
    return conductivity(boost::units::quantity<boost::units::si::dimensionless>(salinity),
                        temperature, pressure);
}

} // namespace seawater
} // namespace util
} // namespace goby

#endif
