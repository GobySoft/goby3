// Copyright 2009-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Chris Murphy <cmurphy@bluefinrobotics.com>
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

#include <boost/algorithm/string.hpp>

#include "goby/util/binary.h"

#include "nmea_sentence.h"

bool goby::util::NMEASentence::enforce_talker_length = true;

goby::util::NMEASentence::NMEASentence(std::string s, strategy cs_strat /*= VALIDATE*/)
{
    bool found_csum = false;
    unsigned int cs;
    // Silently drop leading/trailing whitespace if present.
    boost::trim(s);
    // Basic error checks ($, empty)
    if (s.empty())
        throw bad_nmea_sentence("NMEASentence: no message provided.");
    if (s[0] != '$' && s[0] != '!')
        throw bad_nmea_sentence("NMEASentence: no $ or !: '" + s + "'.");
    // Check if the checksum exists and is correctly placed, and strip it.
    // If it's not correctly placed, we'll interpret it as part of message.
    // NMEA spec doesn't seem to say that * is forbidden elsewhere? (should be)
    if (s.size() > 3 && s.at(s.size() - 3) == '*')
    {
        std::string hex_csum = s.substr(s.size() - 2);
        found_csum = util::hex_string2number(hex_csum, cs);
        s = s.substr(0, s.size() - 3);
    }
    // If we require a checksum and haven't found one, fail.
    if (cs_strat == REQUIRE and !found_csum)
        throw bad_nmea_sentence("NMEASentence: no checksum: '" + s + "'.");
    // If we found a bad checksum and we care, fail.
    if (found_csum && (cs_strat == REQUIRE || cs_strat == VALIDATE))
    {
        unsigned char calc_cs = NMEASentence::checksum(s);
        if (calc_cs != cs)
            throw bad_nmea_sentence("NMEASentence: bad checksum: '" + s + "'.");
    }
    // Split string into parts.
    boost::split(*(std::vector<std::string>*)this, s, boost::is_any_of(","));
    // Validate talker size.
    if (enforce_talker_length && this->front().size() != 6)
        throw bad_nmea_sentence("NMEASentence: bad talker length '" + s + "'.");
}

unsigned char goby::util::NMEASentence::checksum(const std::string& s)
{
    unsigned char csum = 0;

    if (s.empty())
        throw bad_nmea_sentence("NMEASentence::checksum: no message provided.");
    std::string::size_type star = s.find_first_of("*");
    std::string::size_type dollar = s.find_first_of("$!");

    if (dollar == std::string::npos)
        throw bad_nmea_sentence("NMEASentence::checksum: no $ or ! found.");

    if (star == std::string::npos)
        star = s.length();

    for (std::string::size_type i = dollar + 1; i < star; ++i) csum ^= s[i];
    return csum;
}

std::string goby::util::NMEASentence::message_no_cs() const
{
    std::string message = "";

    for (auto it = begin(), n = end(); it < n; ++it) message += *it + ",";

    // kill last ","
    message.resize(message.size() - 1);
    return message;
}

std::string goby::util::NMEASentence::message() const
{
    std::string bare = message_no_cs();
    std::stringstream message;
    unsigned char csum = NMEASentence::checksum(bare);
    message << bare << "*";
    message << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << unsigned(csum);
    return message.str();
}
