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

#ifndef AIS_ENCODE_20191021H
#define AIS_ENCODE_20191021H

#include <cstdint>

#include <boost/dynamic_bitset.hpp>

#include "goby/util/linebasedcomms/nmea_sentence.h"
#include "goby/util/protobuf/ais.pb.h"

namespace goby
{
namespace util
{
namespace ais
{
class EncoderException : public std::runtime_error
{
  public:
    EncoderException(const std::string& what) : std::runtime_error(what) {}
};

class Encoder
{
  public:
    Encoder(goby::util::ais::protobuf::Position pos);
    Encoder(goby::util::ais::protobuf::Voyage voy);

    boost::dynamic_bitset<std::uint8_t> as_bitset() { return bits_; }

    std::vector<std::uint8_t> as_bytes()
    {
        auto bits = as_bitset();
        std::vector<std::uint8_t> bytes(bits.num_blocks());
        boost::to_block_range(bits, bytes.begin());
        return bytes;
    }

    std::vector<NMEASentence> as_nmea();

  private:
    void encode_msg_18(goby::util::ais::protobuf::Position pos);

  private:
    boost::dynamic_bitset<std::uint8_t> bits_;
};

} // namespace ais
} // namespace util
} // namespace goby

#endif
