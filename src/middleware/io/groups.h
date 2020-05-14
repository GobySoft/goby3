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

#ifndef IO_GROUPS_20190718H
#define IO_GROUPS_20190718H

#include "goby/middleware/group.h"

namespace goby
{
namespace middleware
{
namespace io
{
namespace groups
{
constexpr goby::middleware::Group status{"goby::middleware::io::status"};

constexpr goby::middleware::Group mavlink_raw_in{"goby::apps::zeromq::mavlink_raw_in"};
constexpr goby::middleware::Group mavlink_raw_out{"goby::apps::zeromq::mavlink_raw_out"};

constexpr goby::middleware::Group nmea0183_in{"goby::middleware::io::nmea0183_in"};
constexpr goby::middleware::Group nmea0183_out{"goby::middleware::io::nmea0183_out"};

constexpr goby::middleware::Group can_in{"goby::middleware::io::can_in"};
constexpr goby::middleware::Group can_out{"goby::middleware::io::can_out"};


}
} // namespace io
} // namespace middleware
} // namespace goby

#endif
