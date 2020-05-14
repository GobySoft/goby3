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

#ifndef TIME_TYEPDEFS_20190530H
#define TIME_TYEPDEFS_20190530H

#include <boost/units/quantity.hpp>
#include <boost/units/systems/si/prefixes.hpp>
#include <boost/units/systems/si/time.hpp>

namespace goby
{
namespace time
{
/// \brief microsecond unit
using MicroTimeUnit = decltype(boost::units::si::micro* boost::units::si::seconds);
/// \brief quantity of microseconds (using int64)
using MicroTime = boost::units::quantity<MicroTimeUnit, std::int64_t>;
/// \brief quantity of seconds (using double)
using SITime = boost::units::quantity<boost::units::si::time, double>;

} // namespace time
} // namespace goby

#endif
