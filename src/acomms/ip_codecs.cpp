// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "ip_codecs.h"

#include <netinet/in.h>
#include <arpa/inet.h>

dccl::uint32 goby::acomms::IPv4AddressCodec::pre_encode(const std::string& field_value)
{
    in_addr addr;
    int ret = inet_aton(field_value.c_str(), &addr);
    return (ret == 0) ? -1 : addr.s_addr;
}
std::string goby::acomms::IPv4AddressCodec::post_decode(const dccl::uint32& wire_value)
{
    in_addr addr;
    addr.s_addr = wire_value;
    return std::string(inet_ntoa(addr));
}

uint16_t goby::acomms::net_checksum(const std::string& data)
{
    uint32_t sum = 0;  
    int len = data.size();
    uint16_t* p = (uint16_t*) &data[0];

    while(len > 1)
    {
        sum += ntohs(*(p++));
        len -= 2;
    }

    // last byte is large byte (LSB is padded with zeros)
    if(len)
        sum += (*(uint8_t*)p << 8) & 0xFF00;
            
    while(sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
            
    return ~sum;
}
