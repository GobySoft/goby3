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

#ifndef DEPTHH
#define DEPTHH

#include <cmath>

#include <boost/units/quantity.hpp>
#include <boost/units/systems/angle/degrees.hpp>
#include <boost/units/systems/si.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include "units.h"

namespace goby
{
namespace util
{
namespace seawater
{
/// \brief Calculates depth from pressure and latitude
/// Adapted from "Algorithms for computation of fundamental properties of seawater; UNESCO technical papers in marine science; Vol.:44; 1983"
/// https://unesdoc.unesco.org/ark:/48223/pf0000059832
/// \param pressure Pressure
/// \param latitude Latitude
/// \return computed depth
template <typename PressureUnit = decltype(boost::units::si::deci* bar),
          typename LatitudeUnit = boost::units::degree::plane_angle>
boost::units::quantity<boost::units::si::length>
depth(boost::units::quantity<PressureUnit> pressure, boost::units::quantity<LatitudeUnit> latitude)
{
    using namespace boost::units;

    double P = quantity<decltype(si::deci * bar)>(pressure).value();
    double LAT = quantity<degree::plane_angle>(latitude).value();

    double X = std::sin(LAT / 57.29578);
    X = X * X;
    // GR= GRAVITY VARIATION WITH LATITUDE: ANON (1970) BULLETIN GEODESIQUE
    double GR = 9.780318 * (1.0 + (5.2788E-3 + 2.36E-5 * X) * X) + 1.092E-6 * P;
    double DEPTH = (((-1.82E-15 * P + 2.279E-10) * P - 2.2512E-5) * P + 9.72659) * P;
    DEPTH = DEPTH / GR;

    return DEPTH * si::meters;
}
} // namespace seawater
} // namespace util
} // namespace goby

#endif
