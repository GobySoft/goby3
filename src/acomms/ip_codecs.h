// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef IPCodecs20160329H
#define IPCodecs20160329H

#include <cstdint>

#include "dccl.h"

#include "goby/acomms/protobuf/network_header.pb.h"
#include "goby/util/primitive_types.h"

namespace goby
{
namespace acomms
{
// 32 bit IPv4 address
class IPv4AddressCodec : public dccl::TypedFixedFieldCodec<std::uint32_t, std::string>
{
  private:
    std::uint32_t pre_encode(const std::string& field_value);
    std::string post_decode(const std::uint32_t& wire_value);

    dccl::Bitset encode() { return dccl::Bitset(size(), 0); }
    dccl::Bitset encode(const std::uint32_t& wire_value)
    {
        return dccl::Bitset(size(), wire_value);
    }
    std::uint32_t decode(dccl::Bitset* bits) { return bits->to<std::uint32_t>(); }
    unsigned size() { return 32; }
};

// 16 bit IP unsigned short codec
class NetShortCodec : public dccl::TypedFixedFieldCodec<std::uint32_t>
{
  private:
    dccl::Bitset encode() { return dccl::Bitset(size(), 0); }

    dccl::Bitset encode(const std::uint32_t& wire_value)
    {
        unsigned short val = wire_value & 0xFFFF;
        unsigned short val_le = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
        return dccl::Bitset(size(), val_le);
    }
    std::uint32_t decode(dccl::Bitset* bits)
    {
        unsigned short val = bits->to<std::uint32_t>();
        return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
    }
    unsigned size() { return 16; }
};

// 16 bit IP unsigned short codec
class IPv4FlagsFragOffsetCodec
    : public dccl::TypedFixedFieldCodec<goby::acomms::protobuf::IPv4Header::FlagsFragOffset>
{
  private:
    dccl::Bitset encode() { return dccl::Bitset(size(), 0); }

    dccl::Bitset encode(const goby::acomms::protobuf::IPv4Header::FlagsFragOffset& wire_value)
    {
        unsigned short val = wire_value.fragment_offset() & 0x1FFF; // 13 bits
        if (wire_value.dont_fragment())
            val |= 0x4000;
        if (wire_value.more_fragments())
            val |= 0x2000;
        unsigned short val_le = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
        return dccl::Bitset(size(), val_le);
    }
    goby::acomms::protobuf::IPv4Header::FlagsFragOffset decode(dccl::Bitset* bits)
    {
        unsigned short val_le = bits->to<std::uint32_t>();
        unsigned short val = ((val_le & 0xFF) << 8) | ((val_le >> 8) & 0xFF);
        goby::acomms::protobuf::IPv4Header::FlagsFragOffset ret;
        ret.set_fragment_offset(val & 0x1FFF);
        ret.set_dont_fragment(val & 0x4000);
        ret.set_more_fragments(val & 0x2000);
        return ret;
    }
    unsigned size() { return 16; }
};

// no-op identifier codec
template <unsigned Id>
class IPGatewayEmptyIdentifierCodec : public dccl::TypedFixedFieldCodec<std::uint32_t>
{
  private:
    dccl::Bitset encode() { return dccl::Bitset(0, 0); }
    dccl::Bitset encode(const std::uint32_t& wire_value) { return dccl::Bitset(0, 0); }
    std::uint32_t decode(dccl::Bitset* bits) { return Id; }
    unsigned size() { return 0; }
};

uint16_t net_checksum(const std::string& data);

} // namespace acomms
} // namespace goby

#endif
