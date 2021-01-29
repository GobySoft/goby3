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

#ifndef GOBY_ACOMMS_ACOMMS_CONSTANTS_H
#define GOBY_ACOMMS_ACOMMS_CONSTANTS_H

#include <bitset>
#include <limits>
#include <string>

/// The global namespace for the Goby project
namespace goby
{
/// Classes and functions pertaining to acoustic communications (acomms) as well as related marine relevant communications links (such as satellite)
namespace acomms
{
constexpr unsigned BITS_IN_BYTE{8};
/// \brief One hex char is a nibble (4 bits), two nibbles per byte
constexpr unsigned NIBS_IN_BYTE{2};

/// special modem id for the broadcast destination - no one is assigned this address. Analogous to internet protocol address 192.168.1.255 on a 192.168.1.0/24 subnet
constexpr int BROADCAST_ID{0};

/// special modem id used internally to goby-acomms for indicating that the MAC layer (\c amac) is agnostic to the next destination. The next destination is thus set by the data provider (typically QueueManager or DynamicBuffer)
constexpr int QUERY_DESTINATION_ID{-1};

/// similar to QUERY_DESTINATION_ID but for the source modem id
constexpr int QUERY_SOURCE_ID{-1};

} // namespace acomms
} // namespace goby
#endif
