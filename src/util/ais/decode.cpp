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

#include "decode.h"

goby::util::ais::Decoder::Decoder(std::vector<NMEASentence> nmeas)
{
    for (const auto& nmea : nmeas) push(nmea);
}

bool goby::util::ais::Decoder::push(goby::util::NMEASentence nmea)
{
    if (complete())
        throw(DecoderException("Message already decoded, no more NMEA lines required."));

    if (!ais_stream_decoder_.AddLine(nmea.message()))
        throw(DecoderException("NMEA sentence unused: " + nmea.message()));

    if (ais_stream_decoder_.size() > 0)
    {
        ais_msg_ = ais_stream_decoder_.PopOldestMessage();

        switch (parsed_type())
        {
            case ParsedType::POSITION: decode_position(); break;
            case ParsedType::VOYAGE: decode_voyage(); break;
            default: break;
        }
    }

    return complete();
}

goby::util::ais::protobuf::Voyage goby::util::ais::Decoder::as_voyage()
{
    if (parsed_type() != ParsedType::VOYAGE)
        throw(DecoderException("Invalid message type " + std::to_string(message_id()) +
                               " for Voyage"));

    return voy_;
}

goby::util::ais::protobuf::Position goby::util::ais::Decoder::as_position()
{
    if (parsed_type() != ParsedType::POSITION)
        throw(DecoderException("Invalid message type " + std::to_string(message_id()) +
                               " for Position"));

    return pos_;
}

void goby::util::ais::Decoder::decode_position()
{
    using goby::util::ais::protobuf::Position;
    using goby::util::ais::protobuf::Status;
    using namespace boost::units;

    switch (message_id())
    {
        case 1:
        case 2:
        case 3:
        {
            const auto& ais = dynamic_cast<const libais::Ais1_2_3&>(as_libais_msg());
            set_shared_fields(pos_, ais);
            if (goby::util::ais::protobuf::Status_IsValid(ais.nav_status))
                pos_.set_nav_status(static_cast<Status>(ais.nav_status));
            switch (ais.rot_raw)
            {
                case 0:
                    pos_.set_turn_info(Position::TURN_INFO__NOT_TURNING);
                    pos_.set_turn_rate_with_units(ais.rot * degree::degrees / si::seconds);
                    break;
                case 0x80:
                case 127:
                case -127: pos_.set_turn_info(static_cast<Position::TurnInfo>(ais.rot_raw)); break;
                default:
                    pos_.set_turn_info(Position::TURN_INFO__TURNING_AT_GIVEN_RATE);
                    pos_.set_turn_rate_with_units(ais.rot * degree::degrees / si::seconds);
                    break;
            }

            if (Position::PositionAccuracy_IsValid(ais.position_accuracy))
                pos_.set_position_accuracy(
                    static_cast<Position::PositionAccuracy>(ais.position_accuracy));
        }

        break;

        case 18:
        {
            const auto& ais = dynamic_cast<const libais::Ais18&>(as_libais_msg());
            set_shared_fields(pos_, ais);
        }
        break;

        case 19:
        {
            const auto& ais = dynamic_cast<const libais::Ais19&>(as_libais_msg());
            set_shared_fields(pos_, ais);
        }
        break;

        default: break;
    }
}

void goby::util::ais::Decoder::decode_voyage()
{
    // todo
}
