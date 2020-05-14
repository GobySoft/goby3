// Copyright 2017-2020:
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

#include "waveglider_sv2_frontseat_driver.pb.h"
#include <dccl.h>
#include <dccl/field_codec_id.h>

extern "C"
{
    void dccl3_load(dccl::Codec* dccl);
    void dccl3_unload(dccl::Codec* dccl);
}

namespace goby
{
namespace moos
{
class SV2IdentifierCodec : public dccl::DefaultIdentifierCodec
{
  private:
    dccl::Bitset encode() { return encode(0); }

    dccl::Bitset encode(const dccl::uint32& wire_value)
    {
        return dccl::Bitset(size(), wire_value - 0x7E0000);
    }

    dccl::uint32 decode(dccl::Bitset* bits) { return 0x7E0000 + bits->to<dccl::uint32>(); }

    unsigned size() { return 2 * dccl::BITS_IN_BYTE; }

    unsigned size(const dccl::uint32& field_value) { return size(); }

    unsigned max_size() { return size(); }

    unsigned min_size() { return size(); }
};

template <typename Integer> class SV2NumericCodec : public dccl::TypedFixedFieldCodec<Integer>
{
  private:
    unsigned size()
    {
        dccl::uint64 v = dccl::FieldCodecBase::dccl_field_options().max() + 1;
        unsigned r = 0;
        while (v >>= 1) r++;
        return r;
    }

    dccl::Bitset encode() { return dccl::Bitset(size()); }

    // this works because both DCCL and the SV2 protocol uses little-endian representation
    dccl::Bitset encode(const Integer& i)
    {
        dccl::Bitset b;
        b.from<Integer>(i, size());
        return b;
    }

    Integer decode(dccl::Bitset* bits) { return bits->to<Integer>(); }

    void validate() {}
};
} // namespace moos
} // namespace goby
