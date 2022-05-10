// Copyright 2019-2022:
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
// along with Goby.  If not, see <http://www.gnu.org/lic

#include <algorithm> // for max
#include <cassert>   // for assert

#include <boost/lexical_cast/bad_lexical_cast.hpp> // for bad_lexical_cast
#include <boost/units/operators.hpp>               // for units
#include <boost/units/unit.hpp>                    // for unit

#include "encode.h"
#include "goby/util/linebasedcomms/nmea_sentence.h" // for NMEASentence
#include "goby/util/protobuf/ais.pb.h"              // for Position, Voyage

std::atomic<int> goby::util::ais::Encoder::sequence_id_{0};

goby::util::ais::Encoder::Encoder(const goby::util::ais::protobuf::Position& pos)
{
    switch (pos.message_id())
    {
        case 1:
        case 2:
        case 3:
        case 19:
            throw(EncoderException("Message type: " + std::to_string(pos.message_id()) +
                                   " is not yet supported by Encoder"));

        case 18: encode_msg_18(pos); break;

        default:
            throw(EncoderException("Message type: " + std::to_string(pos.message_id()) +
                                   " is not valid for Position (must be 1, 2, 3, 18, or 19)"));
    }
}

goby::util::ais::Encoder::Encoder(const goby::util::ais::protobuf::Voyage& voy, int part_num)
{
    switch (voy.message_id())
    {
        case 5:
            throw(EncoderException("Message type: " + std::to_string(voy.message_id()) +
                                   " is not yet supported by Encoder"));
        case 24: encode_msg_24(voy, part_num); break;

        default:
            throw(EncoderException("Message type: " + std::to_string(voy.message_id()) +
                                   " is not valid for Voyage (must be 5 or 24)"));
    }
}

std::vector<goby::util::NMEASentence> goby::util::ais::Encoder::as_nmea() const
{
    constexpr int max_nmea0183_bytes = 82;
    constexpr int ais_overhead_bytes = 20;
    constexpr int cr_lf_bytes = 2;
    constexpr int ais_bits_per_block = 6;
    constexpr int max_bits_per_payload =
        (max_nmea0183_bytes - ais_overhead_bytes - cr_lf_bytes) * ais_bits_per_block;

    int bits_size = bits_.size();
    int number_payloads =
        bits_size / max_bits_per_payload + (bits_size % max_bits_per_payload != 0);

    std::vector<goby::util::NMEASentence> nmeas;
    for (int payload_i = 0; payload_i < number_payloads; ++payload_i)
    {
        NMEASentence nmea("!AIVDM", NMEASentence::IGNORE);
        nmea.push_back(number_payloads);
        nmea.push_back(payload_i + 1);
        if (number_payloads > 1)
            nmea.push_back(static_cast<int>(sequence_id_));
        else
            nmea.push_back("");
        if (channel_ == RadioChannel::CLASS_A)
            nmea.push_back("A");
        else
            nmea.push_back("B");

        int pad_bits = bits_size % ais_bits_per_block;
        int number_blocks = bits_size / ais_bits_per_block + (pad_bits != 0);

        std::string payload;

        boost::dynamic_bitset<> mask(bits_.size(), 0x3F);
        for (int block_i = 0; block_i < number_blocks; ++block_i)
        {
            // 6-bit unsigned int
            auto raw_value =
                ((bits_ >> ais_bits_per_block * (number_blocks - block_i - 1)) & mask).to_ulong();
            // ascii armored
            auto ascii_value = raw_value + '0';
            if (ascii_value > 'W')
                ascii_value += ('`' - 'W' - 1); // skip values between 'W' and '`'

            payload.push_back(ascii_value);
        }
        nmea.push_back(payload);
        nmea.push_back(pad_bits);

        nmeas.push_back(nmea);
    }
    if (number_payloads > 1)
    {
        sequence_id_++;
        if (sequence_id_ > 9)
            sequence_id_ = 0;
    }

    return nmeas;
}

void goby::util::ais::Encoder::encode_msg_18(const goby::util::ais::protobuf::Position& pos)
{
    using namespace boost::units;

    channel_ = RadioChannel::CLASS_B;

    // len, uint value, string value, is_string
    std::vector<AISField> fields{
        {6, 18},                                             // message type
        {2},                                                 // repeat indicator
        {30, static_cast<std::uint32_t>(pos.mmsi())},        // mmsi
        {8},                                                 // regional reserved
        {10, ais_speed(pos.speed_over_ground_with_units())}, // sog in 1/10 knots
        {1, static_cast<std::uint32_t>(
                pos.position_accuracy())},      // position accuracy (0 = GNSS fix), (1 = DGPS)
        {28, ais_latlon(pos.lon_with_units())}, // 1/10000 minutes
        {27, ais_latlon(pos.lat_with_units())}, // same as lon
        {12, pos.has_course_over_ground() ? ais_angle(pos.course_over_ground_with_units(), 1)
                                          : 3600}, // cog in 0.1 degrees
        {9, pos.has_true_heading() ? ais_angle(pos.true_heading_with_units(), 0)
                                   : 511},                    // heading in 1 degree
        {6, static_cast<std::uint32_t>(pos.report_second())}, // report sec
        {2},                                                  // regional reserved
        {1, 1},                                               // CS Unit,  1 = Class B "CS" unit
        {1},                                                  // Display flag
        {1},                                                  // DSC flag
        {1},                                                  // Band flag
        {1},                                                  // Message 22 flag
        {1},                                                  // Assigned mode
        {1, pos.raim()},                                      // RAIM
        {1, 1},                                               // (always "1" for Class-B "CS")
        {19,
         393222} // Because Class B "CS" does not use any Communication State information, this field shall be filled with the following value: 1100000000000000110.
    };

    concatenate_bitset(fields);
    assert(bits_.size() == 168);
}

void goby::util::ais::Encoder::encode_msg_24(const goby::util::ais::protobuf::Voyage& voy,
                                             std::uint32_t part_num)
{
    channel_ = RadioChannel::CLASS_B;

    if (part_num == 0)
    {
        std::vector<AISField> fields{
            {6, 24},                                      // message type
            {2},                                          // repeat indicator
            {30, static_cast<std::uint32_t>(voy.mmsi())}, // mmsi
            {2, part_num},                                // part num
            {120, 0, voy.name(), true},                   // name
            {8}                                           // spare
        };

        concatenate_bitset(fields);
        assert(bits_.size() == 168);
    }
    else
    {
        std::vector<AISField> fields{
            {6, 24},                                      // message type
            {2},                                          // repeat indicator
            {30, static_cast<std::uint32_t>(voy.mmsi())}, // mmsi
            {2, part_num},                                // part num
            {8, static_cast<std::uint32_t>(voy.type())},  // ship type
            {18, 0, "XXX", true},                         // vendor ID
            {4, 0},                                       // unit model code
            {20, 0},                                      // serial number
            {42, 0, voy.callsign(), true},                // callsign
            {9, voy.to_bow()},                            // dimA
            {9, voy.to_stern()},                          // dimB
            {6, voy.to_port()},                           // dimC
            {6, voy.to_starboard()},                      // dimD
            {6}                                           // spare
        };

        concatenate_bitset(fields);
        assert(bits_.size() == 168);
    }
}
