// Copyright 2024:
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

#ifndef GOBY_MIDDLEWARE_ACOMMS_GROUPS_H
#define GOBY_MIDDLEWARE_ACOMMS_GROUPS_H

#include "goby/middleware/group.h"

namespace goby
{
namespace middleware
{
namespace acomms
{
namespace groups
{
// used by apps in zeromq/acomms - ported from Goby2
// New applications will generally use those in middleware/transport/intervehicle/groups.h

constexpr Group data_request{"goby::middleware::acomms::data_request"};
constexpr Group data_response{"goby::middleware::acomms::data_response"};

constexpr Group status{"goby::middleware::acomms::status"};

constexpr Group rx{"goby::middleware::acomms::rx"};
constexpr Group tx{"goby::middleware::acomms::tx"};

constexpr Group queue_push{"goby::middleware::acomms::queue_push"};
constexpr Group queue_rx{"goby::middleware::acomms::queue_rx"};
constexpr Group queue_ack_orig{"goby::middleware::acomms::queue_ack_orig"};

constexpr Group store_server_request{"goby::middleware::acomms::store_server_request"};
constexpr Group store_server_response{"goby::middleware::acomms::store_server_response"};

} // namespace groups
} // namespace acomms
} // namespace middleware
} // namespace goby

#endif
