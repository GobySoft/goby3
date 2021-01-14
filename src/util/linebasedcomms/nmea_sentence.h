// Copyright 2009-2020:
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

#ifndef GOBY_UTIL_LINEBASEDCOMMS_NMEA_SENTENCE_H
#define GOBY_UTIL_LINEBASEDCOMMS_NMEA_SENTENCE_H

#include <algorithm> // for max
#include <memory>    // for allocator_trait...
#include <sstream>   // for ostream
#include <stdexcept> // for runtime_error
#include <string>    // for string, operator+
#include <vector>    // for vector

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF, is_...
#include <boost/algorithm/string/split.hpp>          // for split

#include "goby/util/as.h" // for as

namespace goby
{
namespace util
{
// simple exception class
class bad_nmea_sentence : public std::runtime_error
{
  public:
    bad_nmea_sentence(const std::string& s) : std::runtime_error(s) {}
};

class NMEASentence : public std::vector<std::string>
{
  public:
    enum strategy
    {
        IGNORE,
        VALIDATE,
        REQUIRE
    };

    NMEASentence() = default;
    NMEASentence(std::string s, strategy cs_strat = VALIDATE);

    // Bare message, no checksum or \r\n
    std::string message_no_cs() const;

    // Includes checksum, but no \r\n
    std::string message() const;

    // Includes checksum and \r\n
    std::string message_cr_nl() const { return message() + "\r\n"; }

    // first two talker (CC)
    std::string talker_id() const { return empty() ? "" : front().substr(1, 2); }

    // last three (CFG)
    std::string sentence_id() const { return empty() ? "" : front().substr(3); }

    template <typename T> T as(int i) const { return goby::util::as<T>(at(i)); }

    template <typename T> void push_back(T t) { push_back(goby::util::as<std::string>(t)); }

    // necessary when pushing back string "foo,bar" that contain
    // commas
    void push_back(const std::string& str)
    {
        if (str.find(',') == std::string::npos)
        {
            std::vector<std::string>::push_back(str);
            return;
        }
        else
        {
            std::vector<std::string> vec;
            boost::split(vec, str, boost::is_any_of(","));

            for (const std::string& s : vec) std::vector<std::string>::push_back(s);
        }
    }

    static unsigned char checksum(const std::string& s);

    static bool enforce_talker_length;
};
} // namespace util
} // namespace goby

// overloaded <<
inline std::ostream& operator<<(std::ostream& out, const goby::util::NMEASentence& nmea)
{
    out << nmea.message();
    return out;
}

#endif
