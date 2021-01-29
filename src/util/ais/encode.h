// Copyright 2011-2021:
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

#ifndef GOBY_UTIL_AIS_ENCODE_H
#define GOBY_UTIL_AIS_ENCODE_H

#include <atomic>    // for atomic
#include <cmath>     // for round
#include <cstdint>   // for uint32_t, uint8_t
#include <stdexcept> // for runtime_error
#include <string>    // for string, allocator
#include <vector>    // for vector

#include <boost/algorithm/string/case_conv.hpp>    // for to_upper
#include <boost/dynamic_bitset/dynamic_bitset.hpp> // for dynamic_bitset
#include <boost/units/base_unit.hpp>               // for base_unit<>::unit...
#include <boost/units/base_units/metric/knot.hpp>  // for knot_base_unit
#include <boost/units/quantity.hpp>                // for quantity, operator*
#include <boost/units/systems/angle/degrees.hpp>   // for plane_angle, degrees

namespace goby
{
namespace util
{
class NMEASentence;

namespace ais
{
namespace protobuf
{
class Position;
class Voyage;
} // namespace protobuf

class EncoderException : public std::runtime_error
{
  public:
    EncoderException(const std::string& what) : std::runtime_error(what) {}
};

class Encoder
{
  public:
    Encoder(const goby::util::ais::protobuf::Position& pos);
    Encoder(const goby::util::ais::protobuf::Voyage& voy, int part_num = 0);

    boost::dynamic_bitset<> as_bitset() const { return bits_; }

    std::vector<NMEASentence> as_nmea() const;

  private:
    void encode_msg_18(const goby::util::ais::protobuf::Position& pos);
    void encode_msg_24(const goby::util::ais::protobuf::Voyage& voy, std::uint32_t part_num);

    boost::units::quantity<boost::units::degree::plane_angle>
    wrap_0_360(boost::units::quantity<boost::units::degree::plane_angle> in)
    {
        auto full_revolution = 360 * boost::units::degree::degrees;
        while (in < 0 * boost::units::degree::degrees) in += full_revolution;
        while (in >= full_revolution) in -= full_revolution;
        return in;
    }

    // lat/lon as 1/10000 minutes
    template <typename AngleType, typename ValueType>
    std::uint32_t ais_latlon(boost::units::quantity<AngleType, ValueType> ll)
    {
        return std::round(boost::units::quantity<boost::units::degree::plane_angle>(ll).value() *
                          600000.0);
    }

    // 0 - 360 as tenths
    template <typename AngleType, typename ValueType>
    std::uint32_t ais_angle(boost::units::quantity<AngleType, ValueType> a, int precision)
    {
        return std::round(
            wrap_0_360(boost::units::quantity<boost::units::degree::plane_angle>(a)).value() *
            std::pow(10, precision));
    }

    // 1/10 knots
    template <typename SpeedType, typename ValueType>
    std::uint32_t ais_speed(boost::units::quantity<SpeedType, ValueType> s)
    {
        return std::round(
            boost::units::quantity<boost::units::metric::knot_base_unit::unit_type>(s).value() *
            10);
    }

    struct AISField;

    void concatenate_bitset(const std::vector<AISField>& fields)
    {
        for (auto it = fields.rbegin(), end = fields.rend(); it != end; ++it)
        {
            auto fb = it->as_bitset();
            for (int i = 0, n = fb.size(); i < n; ++i) bits_.push_back(fb[i]);
        }
    }

  private:
    struct AISField
    {
        std::uint8_t len{0};
        std::uint32_t u{0};
        std::string s{};
        bool is_string{false};

        boost::dynamic_bitset<> as_bitset() const
        {
            if (is_string)
            {
                constexpr int ais_bits_per_char = 6;
                std::string ms = s;
                ms.resize(len / ais_bits_per_char, '@');
                boost::to_upper(ms);

                boost::dynamic_bitset<> bits(len, 0);
                for (int i = 0, n = ms.size(); i < n; ++i)
                {
                    char c = ms[i];

                    if (c >= '@' && c <= '_')
                        c -= '@';
                    else if (c >= ' ' && c <= '?')
                        ; // no change from ascii value
                    else  // other values that can't be represented
                        c = '@';

                    boost::dynamic_bitset<> char_bits(len, c & 0x3F);
                    bits |= (char_bits << (n - i - 1) * ais_bits_per_char);
                }
                return bits;
            }
            else
            {
                return boost::dynamic_bitset<>(len, u);
            }
        }
    };

    boost::dynamic_bitset<> bits_;
    enum class RadioChannel
    {
        CLASS_A,
        CLASS_B
    };
    RadioChannel channel_{RadioChannel::CLASS_B};

    static std::atomic<int> sequence_id_;
};

} // namespace ais
} // namespace util
} // namespace goby

#endif
