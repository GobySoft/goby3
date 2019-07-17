// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
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

#include "goby/middleware/marshalling/interface.h"
#include <vector>

namespace goby
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

template <typename DataType> struct SerializerParserHelper<DataType, MyMarshallingScheme::DEQUECHAR>
{
    static std::vector<char> serialize(const DataType& msg)
    {
        std::vector<char> bytes(msg.begin(), msg.end());
        return bytes;
    }

    static std::string type_name() { return "DEQUECHAR"; }

    static DataType parse(const std::vector<char>& bytes)
    {
        if (bytes.size())
            DataType msg(bytes.begin(), bytes.end() - 1);
    }
};

template <typename T>
constexpr int scheme(typename std::enable_if<std::is_same<T, std::deque<char> >::value>::type* = 0)
{
    return MyMarshallingScheme::DEQUECHAR;
}
} // namespace middleware
} // namespace goby
