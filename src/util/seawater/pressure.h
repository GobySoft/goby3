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

// modified for C++ by s. petillo spetillo@mit.edu
// ocean engineering graduate student - mit / whoi joint program
// massachusetts institute of technology (mit)
// laboratory for autonomous marine sensing systems (lamss)

#ifndef PRESSUREH
#define PRESSUREH

#include <cmath>

#include "goby/util/constants.h"

// Calculate water density anomaly at a given Salinity, Temperature, Pressure using the seawater Equation of State.
// Taken directly from MATLAB OceansToolbox pressure.m

namespace goby
{
namespace util
{
namespace seawater
{
/// \brief Calculates pressure from depth and latitude
///
/// Ref: Saunders, "Practical Conversion of Pressure to Depth", J. Phys. Oceanog., April 1981.
/// \param depth Depth
/// \param latitude Latitude
/// \return computed pressure
template <typename DepthUnit = boost::units::si::length,
          typename LatitudeUnit = boost::units::degree::plane_angle>
inline boost::units::quantity<decltype(boost::units::si::deci * bar)>
pressure(boost::units::quantity<DepthUnit> depth, boost::units::quantity<LatitudeUnit> latitude)
{
    // function P80=pressure(DPTH,XLAT);
    /*
      Computes pressure given the depth at some latitude
      P = depth2pressure(DPTH,XLAT) gives the pressure P (dbars) at a depth DPTH (m) at some latitude Xdecltype(boost::units::si::deci* bar)LAT (degrees).

      This probably works best in mid-latitude oceans, if anywhere!

      Ref: Saunders, "Practical Conversion of Pressure to Depth",
            J. Phys. Oceanog., April 1981.

      I copied this from the Matlab OceansToolbox pressure.m, copied directly from the UNESCO algorithms.


      CHECK VALUE: P80=7500.004 DBARS;FOR LAT=30 DEG., DEPTH=7321.45 METERS
                              ^P80 test value should be 7500.006 (from spetillo@mit.edu)
    */
    using namespace boost::units;

    double DPTH = quantity<boost::units::si::length>(depth).value();
    double XLAT = quantity<degree::plane_angle>(latitude).value();

    const double pi = goby::util::pi<double>;

    double PLAT = std::abs(XLAT * pi / 180);
    double D = std::sin(PLAT);
    double C1 = (5.92E-3) + (D * D) * (5.25E-3);
    double P80 = ((1 - C1) - sqrt(((1 - C1) * (1 - C1)) - ((8.84E-6) * DPTH))) / 4.42E-6;

    return P80 * boost::units::si::deci * bar;
}
} // namespace seawater
} // namespace util
} // namespace goby

#endif
