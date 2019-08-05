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

#ifndef RPM_SYSTEM_20190727H
#define RPM_SYSTEM_20190727H

#include <boost/units/base_units/metric/minute.hpp>
#include <boost/units/make_system.hpp>
#include <boost/units/unit.hpp>
#include <boost/units/conversion.hpp>
#include <boost/units/physical_dimensions/frequency.hpp>
#include <boost/units/physical_dimensions/angular_velocity.hpp>
#include <boost/units/static_constant.hpp>
#include <boost/units/systems/angle/revolutions.hpp>

#include "goby/util/constants.h"

namespace goby
{
namespace util
{
namespace units
{
namespace rpm
{
typedef boost::units::make_system<boost::units::angle::revolution_base_unit, boost::units::metric::minute_base_unit>::type system;

typedef boost::units::unit<boost::units::dimensionless_type, system> dimensionless;

// an RPM is either 1/60 Hz (when considered a frequency)
typedef boost::units::unit<boost::units::frequency_dimension, system> frequency;
// or 2*pi/60 rad/s (when considered an angular velocity)
typedef boost::units::unit<boost::units::angular_velocity_dimension, system> angular_velocity;

BOOST_UNITS_STATIC_CONSTANT(rpm_f, frequency);
BOOST_UNITS_STATIC_CONSTANT(rpms_f, frequency);

BOOST_UNITS_STATIC_CONSTANT(rpm_omega, angular_velocity);
BOOST_UNITS_STATIC_CONSTANT(rpms_omega, angular_velocity);

} // namespace rpm
} // namespace units
} // namespace util
} // namespace goby

#endif
