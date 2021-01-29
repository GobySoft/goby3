// Copyright 2019-2021:
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

#include "goby/middleware/group.h"

#ifndef GOBY_MIDDLEWARE_FRONTSEAT_GROUPS_H
#define GOBY_MIDDLEWARE_FRONTSEAT_GROUPS_H

namespace goby
{
namespace middleware
{
namespace frontseat
{
namespace groups
{
constexpr goby::middleware::Group node_status{"goby::middleware::frontseat::node_status"};
constexpr goby::middleware::Group desired_course{"goby::middleware::frontseat::desired_course"};

constexpr goby::middleware::Group raw_in{"goby::middleware::frontseat::raw_in"};
constexpr goby::middleware::Group raw_out{"goby::middleware::frontseat::raw_out"};
constexpr goby::middleware::Group raw_send_request{"goby::middleware::frontseat::raw_send_request"};

constexpr goby::middleware::Group command_request{"goby::middleware::frontseat::command_request"};
constexpr goby::middleware::Group command_response{"goby::middleware::frontseat::command_response"};

constexpr goby::middleware::Group data_from_frontseat{
    "goby::middleware::frontseat::data_from_frontseat"};
constexpr goby::middleware::Group data_to_frontseat{
    "goby::middleware::frontseat::data_to_frontseat"};

constexpr goby::middleware::Group helm_state{"goby::middleware::frontseat::helm_state"};

constexpr goby::middleware::Group status{"goby::middleware::frontseat::status"};

} // namespace groups
} // namespace frontseat
} // namespace middleware
} // namespace goby

#endif
