// Copyright 2019-2020:
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

#ifndef GOBY_UTIL_AIS_DECODE_H
#define GOBY_UTIL_AIS_DECODE_H

#include <ais.h>
#include <vdm.h>

#include <boost/units/base_units/metric/knot.hpp>

#include "goby/util/linebasedcomms/nmea_sentence.h"
#include "goby/util/protobuf/ais.pb.h"

namespace goby
{
namespace util
{
namespace ais
{
class DecoderException : public std::runtime_error
{
  public:
    DecoderException(const std::string& what) : std::runtime_error(what) {}
};

class Decoder
{
  public:
    Decoder();
    Decoder(NMEASentence nmea) : Decoder(std::vector<NMEASentence>(1, nmea)) {}
    Decoder(std::vector<NMEASentence> nmeas);

    // returns true if message is complete
    bool push(NMEASentence nmea);

    bool complete() { return ais_msg_ != nullptr; }

    int message_id()
    {
        if (complete())
            return ais_msg_->message_id;
        else
            throw(DecoderException("Message not complete: missing NMEA sentences?"));
    }

    enum class ParsedType
    {
        NOT_SUPPORTED,
        POSITION,
        VOYAGE
    };

    ParsedType parsed_type()
    {
        switch (message_id())
        {
            case 1:
            case 2:
            case 3:
            case 18:
            case 19: return ParsedType::POSITION;
            case 5:
            case 24: return ParsedType::VOYAGE;
            default: return ParsedType::NOT_SUPPORTED;
        }
    }

    goby::util::ais::protobuf::Voyage as_voyage();
    goby::util::ais::protobuf::Position as_position();

    libais::AisMsg& as_libais_msg()
    {
        if (complete())
            return *ais_msg_;
        else
            throw(DecoderException("Message not complete: missing NMEA sentences?"));
    }

  private:
    std::string trim_ais_string(std::string in)
    {
        boost::trim_if(in, boost::algorithm::is_space() || boost::algorithm::is_any_of("@"));
        return in;
    }

    template <typename LibAisMessage>
    void set_shared_fields(goby::util::ais::protobuf::Voyage& voy, const LibAisMessage& ais,
                           int part_num)
    {
        using namespace boost::units;

        voy_.set_message_id(ais.message_id);
        voy_.set_mmsi(ais.mmsi);

        if (part_num == 0)
        {
            std::string name = trim_ais_string(ais.name);
            if (!name.empty())
                voy_.set_name(name);
        }
        else if (part_num == 1)
        {
            std::string callsign = trim_ais_string(ais.callsign);
            if (!callsign.empty())
                voy_.set_callsign(callsign);

            if (protobuf::Voyage::ShipType_IsValid(ais.type_and_cargo))
                voy_.set_type(static_cast<protobuf::Voyage::ShipType>(ais.type_and_cargo));
            voy_.set_to_bow_with_units(ais.dim_a * si::meters);
            voy_.set_to_stern_with_units(ais.dim_b * si::meters);
            voy_.set_to_port_with_units(ais.dim_c * si::meters);
            voy_.set_to_starboard_with_units(ais.dim_d * si::meters);
        }
    }

    template <typename LibAisMessage>
    void set_shared_fields(goby::util::ais::protobuf::Position& pos, const LibAisMessage& ais)
    {
        using namespace boost::units;
        pos.set_message_id(ais.message_id);
        pos.set_mmsi(ais.mmsi);
        metric::knot_base_unit::unit_type knots;
        pos.set_speed_over_ground_with_units(ais.sog * knots);
        pos.set_lat_with_units(ais.position.lat_deg * degree::degrees);
        pos.set_lon_with_units(ais.position.lng_deg * degree::degrees);
        pos.set_course_over_ground_with_units(ais.cog * degree::degrees);
        if (ais.true_heading >= 0 && ais.true_heading < 360)
            pos.set_true_heading_with_units(ais.true_heading * degree::degrees);
        pos.set_report_second_with_units(ais.timestamp * si::seconds);
        pos.set_raim(ais.raim);

        if (protobuf::Position::PositionAccuracy_IsValid(ais.position_accuracy))
            pos_.set_position_accuracy(
                static_cast<protobuf::Position::PositionAccuracy>(ais.position_accuracy));
    }

    void decode_position();
    void decode_voyage();

  private:
    libais::VdmStream ais_stream_decoder_;
    std::unique_ptr<libais::AisMsg> ais_msg_;

    goby::util::ais::protobuf::Voyage voy_;
    goby::util::ais::protobuf::Position pos_;
};

inline ostream& operator<<(ostream& os, Decoder::ParsedType t)
{
    switch (t)
    {
        default:
        case Decoder::ParsedType::NOT_SUPPORTED: return os << "NOT_SUPPORTED";
        case Decoder::ParsedType::VOYAGE: return os << "VOYAGE";
        case Decoder::ParsedType::POSITION: return os << "POSITION";
    }
}

} // namespace ais
} // namespace util
} // namespace goby

#endif
