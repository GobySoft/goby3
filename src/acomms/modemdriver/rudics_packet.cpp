// Copyright 2013-2020:
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

#include <algorithm> // for replace, remove_if
#include <string.h>  // for memcpy

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF, is_...
#include <boost/crc.hpp>                             // for crc_32_type
#include <netinet/in.h>                              // for htonl, ntohl

#include "goby/util/base_convert.h" // for base_convert
#include "rudics_packet.h"

void goby::acomms::serialize_rudics_packet(std::string bytes, std::string* rudics_pkt,
                                           const std::string& reserved, bool include_crc)
{
    if (include_crc)
    {
        // 1. append CRC
        boost::crc_32_type crc;
        crc.process_bytes(bytes.data(), bytes.length());
        bytes += uint32_to_byte_string(crc.checksum());
    }

    // 2. convert to base (256 minus reserved)
    const int reduced_base = 256 - reserved.size();

    goby::util::base_convert(bytes, rudics_pkt, 256, reduced_base);

    // 3. replace reserved characters
    for (int i = 0, n = reserved.size(); i < n; ++i)
    {
        std::replace(rudics_pkt->begin(), rudics_pkt->end(), reserved[i],
                     static_cast<char>(reduced_base + i));
    }

    // 4. append CR
    *rudics_pkt += "\r";
}

void goby::acomms::parse_rudics_packet(std::string* bytes, std::string rudics_pkt,
                                       const std::string& reserved, bool include_crc)
{
    const unsigned CR_SIZE = 1;
    if (rudics_pkt.size() < CR_SIZE)
        throw(RudicsPacketException("Packet too short for <CR>"));

    // 4. remove CR
    rudics_pkt = rudics_pkt.substr(0, rudics_pkt.size() - 1);

    const int reduced_base = 256 - reserved.size();

    // get rid of extra junk
    rudics_pkt.erase(
        std::remove_if(rudics_pkt.begin(), rudics_pkt.end(), boost::is_any_of(reserved)),
        rudics_pkt.end());

    // 3. replace reserved characters
    for (int i = 0, n = reserved.size(); i < n; ++i)
    {
        std::replace(rudics_pkt.begin(), rudics_pkt.end(), static_cast<char>(reduced_base + i),
                     reserved[i]);
    }

    // 2. convert to base
    goby::util::base_convert(rudics_pkt, bytes, reduced_base, 256);

    if (include_crc)
    {
        // 1. check CRC
        const unsigned CRC_BYTE_SIZE = 4;
        if (bytes->size() < CRC_BYTE_SIZE)
            throw(RudicsPacketException("Packet too short for CRC32"));

        std::string crc_str = bytes->substr(bytes->size() - 4, 4);
        uint32_t given_crc = byte_string_to_uint32(crc_str);
        *bytes = bytes->substr(0, bytes->size() - 4);

        boost::crc_32_type crc;
        crc.process_bytes(bytes->data(), bytes->length());
        uint32_t computed_crc = crc.checksum();

        if (given_crc != computed_crc)
            throw(RudicsPacketException("Bad CRC32"));
    }
}

std::string goby::acomms::uint32_to_byte_string(uint32_t i)
{
    union u_t {
        uint32_t i;
        char c[4];
    } u;
    u.i = htonl(i);
    return std::string(u.c, 4);
}

uint32_t goby::acomms::byte_string_to_uint32(const std::string& s)
{
    union u_t {
        uint32_t i;
        char c[4];
    } u;
    memcpy(u.c, s.c_str(), 4);
    return ntohl(u.i);
}
