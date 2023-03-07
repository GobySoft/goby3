// Copyright 2009-2021:
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_INTERVEHICLE_GROUPS_H
#define GOBY_MIDDLEWARE_TRANSPORT_INTERVEHICLE_GROUPS_H

#include "goby/middleware/group.h"

namespace goby
{
namespace middleware
{
namespace intervehicle
{
namespace groups
{
constexpr Group subscription_forward{"goby::middleware::intervehicle::subscription_forward",
                                     Group::broadcast_group};

constexpr Group modem_data_out{"goby::middleware::intervehicle::modem_data_out"};
constexpr Group modem_data_in{"goby::middleware::intervehicle::modem_data_in"};
constexpr Group modem_ack_in{"goby::middleware::intervehicle::modem_ack_in"};
constexpr Group modem_expire_in{"goby::middleware::intervehicle::modem_expire_in"};

constexpr Group modem_subscription_forward_tx{
    "goby::middleware::intervehicle::modem_subscription_forward_tx"};
constexpr Group modem_subscription_forward_rx{
    "goby::middleware::intervehicle::modem_subscription_forward_rx"};
constexpr Group modem_driver_ready{"goby::middleware::intervehicle::modem_driver_ready"};

constexpr Group metadata_request{"goby::middleware::intervehicle::metadata_request"};

constexpr Group subscription_report{"goby::middleware::intervehicle::subscription_report"};

// direct connection to ModemDriverBase signals
constexpr Group modem_receive{"goby::middleware::intervehicle::modem_receive"};
constexpr Group modem_transmit_result{"goby::middleware::intervehicle::modem_transmit_result"};
constexpr Group modem_raw_incoming{"goby::middleware::intervehicle::modem_raw_incoming"};
constexpr Group modem_raw_outgoing{"goby::middleware::intervehicle::modem_raw_outgoing"};

// from calling ModemDriverBase::report()
constexpr Group modem_report{"goby::middleware::intervehicle::modem_report"};

// direct connection to MACManager signals
constexpr Group mac_initiate_transmission{
    "goby::middleware::intervehicle::mac_initiate_transmission"};
constexpr Group mac_slot_start{"goby::middleware::intervehicle::mac_slot_start"};

} // namespace groups
} // namespace intervehicle
} // namespace middleware
} // namespace goby

#endif
