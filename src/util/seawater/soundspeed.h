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

#ifndef SOUNDSPEED_20190606H
#define SOUNDSPEED_20190606H

#include <boost/units/quantity.hpp>
#include <boost/units/systems/si.hpp>
#include <boost/units/systems/temperature/celsius.hpp>

namespace goby
{
namespace util
{
namespace seawater
{
/// K.V. Mackenzie, Nine-term equation for the sound speed in the oceans (1981) J. Acoust. Soc. Am. 70(3), pp 807-812
/// https://doi.org/10.1121/1.386920
/// Ranges of validity encompass: temperature -2 to 30 deg C, salinity 30 to 40, and depth 0 to 8000 m.
/// \param temperature temperature
/// \param salinity salinity (unitless, calculated using Practical Salinity Scale)
/// \param depth depth
/// \throw std::out_of_range if any of the inputs are out of the validity range for this algorithm
/// \return speed of sound in meters per second
template <typename TemperatureUnit = boost::units::celsius::temperature,
          typename DimensionlessUnit = boost::units::si::dimensionless,
          typename LengthUnit = boost::units::si::length>
boost::units::quantity<boost::units::si::velocity>
mackenzie_soundspeed(boost::units::quantity<boost::units::absolute<TemperatureUnit> > temperature,
                     boost::units::quantity<DimensionlessUnit> salinity,
                     boost::units::quantity<LengthUnit> depth)
{
    using namespace boost::units;

    double T = quantity<absolute<celsius::temperature> >(temperature).value();
    double S = quantity<si::dimensionless>(salinity).value();
    double D = quantity<si::length>(depth).value();

    double min_T(-2);
    double max_T(30);

    double min_S(30);
    double max_S(40);

    double min_D(0);
    double max_D(8000);

    if (T < min_T || T > max_T)
        throw std::out_of_range("Temperature not in valid range [-2, 30] deg C");
    if (S < min_S || S > max_S)
        throw std::out_of_range("Salinity not in valid range [30, 40]");
    if (D < min_D || D > max_D)
        throw std::out_of_range("Depth not in valid range [0, 8000] meters");

    return (1448.96 + 4.591 * T - 5.304e-2 * T * T + 2.374e-4 * T * T * T + 1.340 * (S - 35) +
            1.630e-2 * D + 1.675e-7 * D * D - 1.025e-2 * T * (S - 35) - 7.139e-13 * T * D * D * D) *
           si::meters_per_second;
}

/// K.V. Mackenzie, Nine-term equation for the sound speed in the oceans (1981) J. Acoust. Soc. Am. 70(3), pp 807-812 (variant that accepts plain double for salinity)
/// https://doi.org/10.1121/1.386920
/// Ranges of validity encompass: temperature -2 to 30 deg C, salinity 30 to 40, and depth 0 to 8000 m.
/// \param temperature temperature
/// \param salinity salinity
/// \param depth depth
/// \throw std::out_of_range if any of the inputs are out of the validity range for this algorithm
/// \return speed of sound in meters per second
template <typename TemperatureUnit = boost::units::celsius::temperature,
          typename LengthUnit = boost::units::si::length>
boost::units::quantity<boost::units::si::velocity>
mackenzie_soundspeed(boost::units::quantity<boost::units::absolute<TemperatureUnit> > temperature,
                     double salinity, boost::units::quantity<LengthUnit> depth)
{
    return mackenzie_soundspeed(
        temperature, boost::units::quantity<boost::units::si::dimensionless>(salinity), depth);
}
} // namespace seawater
} // namespace util
} // namespace goby

#endif
