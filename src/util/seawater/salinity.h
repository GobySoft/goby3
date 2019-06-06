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

// modified for C++ by s. petillo spetillo@mit.edu
// ocean engineering graduate student - mit / whoi joint program
// massachusetts institute of technology (mit)
// laboratory for autonomous marine sensing systems (lamss)

//Algorithm from S. Petillo salinity.m
// Function for calculation of salinity from conductivity.
//
// S.Petillo (spetillo@mit.edu) - 02 Aug. 2010
//
// Taken directly from:
// 'Algorithms for computation of fundamental properties of seawater'
// UNESCO 1983 equation
// conductivity ratio to practical salinity conversion (SAL78)
//
// Valid over the ranges:
// Temperaturature: -2<T<35 degC
// Salinity: 2<S<42 psu
// (from Prekin and Lewis, 1980)
//
// Units:
// conductivity Cond mS/cm
// salinity S (PSS-78)
// temperature T (IPTS-68) degC
// pressure P decibars

// Cond(35,15,0) = 42.914 mS/cm
// the electrical conductivity of the standard seawater.
// Given by Jan Schultz (23 May, 2008) in
// 'Conversion between conductivity and PSS-78 salinity' at
// http://www.code10.info/index.php?option=com_content&view=category&id=54&Itemid=79

#ifndef SALINITYH
#define SALINITYH

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

template <typename ConductivityUnit = decltype(milli_siemens_per_cm),
          typename TemperatureUnit = boost::units::celsius::temperature,
          typename PressureUnit = decltype(boost::units::si::deci* bar)>
inline boost::units::quantity<boost::units::si::dimensionless>
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

template <typename TemperatureUnit = boost::units::celsius::temperature,
          typename DimensionlessUnit = boost::units::si::dimensionless,
          typename PressureUnit = decltype(boost::units::si::deci* bar)>
inline boost::units::quantity<decltype(milli_siemens_per_cm)>
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

template <typename TemperatureUnit = boost::units::celsius::temperature,
          typename PressureUnit = decltype(boost::units::si::deci* bar)>
inline boost::units::quantity<decltype(milli_siemens_per_cm)>
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
