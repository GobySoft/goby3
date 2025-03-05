// Copyright 2023:
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

#include <algorithm> // for replace, remove_if
#include <cstring>   // for memcpy

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF, is_...
#include <boost/crc.hpp>                             // for crc_32_type
#include <netinet/in.h>                              // for htonl, ntohl

#include "goby/util/base_convert.h" // for base_convert
#include "iridium_rudics_packet.h"  // for uint32_to_byte_string, etc.
#include "iridium_sbd_packet.h"

void goby::acomms::serialize_sbd_packet(std::string bytes, std::string* sbd_pkt, bool include_crc)
{
    if (include_crc)
    {
        // 1. append CRC
        boost::crc_32_type crc;
        crc.process_bytes(bytes.data(), bytes.length());
        bytes += uint32_to_byte_string(crc.checksum());
    }
    *sbd_pkt = bytes;
}

void goby::acomms::parse_sbd_packet(std::string* bytes, std::string sbd_pkt, bool include_crc)
{
    *bytes = sbd_pkt;

    if (include_crc)
    {
        // 1. check CRC
        if (bytes->size() < IRIDIUM_SBD_CRC_BYTE_SIZE)
            throw(SBDPacketException("Packet too short for CRC32"));

        std::string crc_str =
            bytes->substr(bytes->size() - IRIDIUM_SBD_CRC_BYTE_SIZE, IRIDIUM_SBD_CRC_BYTE_SIZE);
        uint32_t given_crc = byte_string_to_uint32(crc_str);
        *bytes = bytes->substr(0, bytes->size() - IRIDIUM_SBD_CRC_BYTE_SIZE);

        boost::crc_32_type crc;
        crc.process_bytes(bytes->data(), bytes->length());
        uint32_t computed_crc = crc.checksum();

        if (given_crc != computed_crc)
            throw(SBDPacketException("Bad CRC32"));
    }
}
