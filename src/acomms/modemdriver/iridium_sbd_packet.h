// Copyright 2009-2023:
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_SBD_PACKET_H
#define GOBY_ACOMMS_MODEMDRIVER_SBD_PACKET_H

#include <cstdint>
#include <stdexcept>
#include <string>

namespace goby
{
namespace acomms
{
class SBDPacketException : public std::runtime_error
{
  public:
    SBDPacketException(const std::string& what) : std::runtime_error(what) {}
};

void serialize_sbd_packet(std::string bytes, std::string* sbd_pkt, bool include_crc = true);
void parse_sbd_packet(std::string* bytes, std::string sbd_pkt, bool include_crc = true);

constexpr int IRIDIUM_SBD_CRC_BYTE_SIZE = 4;
} // namespace acomms
} // namespace goby

#endif
