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

#ifndef SerializeParseString20190717H
#define SerializeParseString20190717H

#include <vector>

#include "interface.h"

namespace goby
{
namespace middleware
{
/// \brief Example usable specialization for std::string using a null terminated array of bytes (C string). Likely not the best choice for production use.
template <> struct SerializerParserHelper<std::string, MarshallingScheme::CSTR>
{
    static std::vector<char> serialize(const std::string& msg)
    {
        std::vector<char> bytes(std::begin(msg), std::end(msg));
        bytes.push_back('\0');
        return bytes;
    }

    static std::string type_name(const std::string& d = std::string()) { return "CSTR"; }

    template <typename CharIterator>
    static std::shared_ptr<std::string> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                              CharIterator& actual_end)
    {
        actual_end = bytes_end;
        if (bytes_begin != bytes_end)
        {
            return std::make_shared<std::string>(bytes_begin, bytes_end - 1);
        }
        else
        {
            return std::make_shared<std::string>();
        }
    }
};

template <typename T, typename std::enable_if<std::is_same<T, std::string>::value>::type* = nullptr>
constexpr int scheme()
{
    return goby::middleware::MarshallingScheme::CSTR;
}
} // namespace middleware
} // namespace goby

#endif
