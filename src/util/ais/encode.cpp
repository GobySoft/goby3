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

#include "encode.h"

goby::util::ais::Encoder::Encoder(goby::util::ais::protobuf::Position pos)
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

goby::util::ais::Encoder::Encoder(goby::util::ais::protobuf::Voyage voy)
{
    switch (voy.message_id())
    {
        case 5:
        case 24:
            throw(EncoderException("Message type: " + std::to_string(voy.message_id()) +
                                   " is not yet supported by Encoder"));

        default:
            throw(EncoderException("Message type: " + std::to_string(voy.message_id()) +
                                   " is not valid for Voyage (must be 5 or 24)"));
    }
}

std::vector<goby::util::NMEASentence> goby::util::ais::Encoder::as_nmea()
{
    std::vector<goby::util::NMEASentence> nmeas;
    NMEASentence nmea;
    nmeas.push_back(nmea);
    return nmeas;
}

void goby::util::ais::Encoder::encode_msg_18(goby::util::ais::protobuf::Position pos) {}
