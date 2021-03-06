// Copyright 2016-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef GOBY_TEST_ZEROMQ_MIDDLEWARE_BASIC_TEST_SCHEME_H
#define GOBY_TEST_ZEROMQ_MIDDLEWARE_BASIC_TEST_SCHEME_H

#include "goby/middleware/marshalling/interface.h"
#include <vector>

namespace goby
{
namespace test
{
namespace middleware
{
struct MyMarshallingScheme
{
    enum MyMarshallingSchemeEnum
    {
        DEQUECHAR = 1000
    };
};
} // namespace middleware
} // namespace test

namespace middleware
{
template <>
struct SerializerParserHelper<std::deque<char>, test::middleware::MyMarshallingScheme::DEQUECHAR>
{
    static std::vector<char> serialize(const std::deque<char>& msg)
    {
        std::vector<char> bytes(msg.begin(), msg.end());
        return bytes;
    }

    static std::string type_name() { return "DEQUECHAR"; }

    static std::string type_name(const std::deque<char>& /*d*/) { return type_name(); }

    static std::deque<char> parse(const std::vector<char>& bytes)
    {
        if (bytes.size())
            return std::deque<char>(bytes.begin(), bytes.end() - 1);
        else
            return std::deque<char>();
    }
};

template <typename T>
constexpr int
scheme(typename std::enable_if<std::is_same<T, std::deque<char>>::value>::type* = nullptr)
{
    return test::middleware::MyMarshallingScheme::DEQUECHAR;
}
} // namespace middleware
} // namespace goby

#endif
