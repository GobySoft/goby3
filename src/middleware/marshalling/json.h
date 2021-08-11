// Copyright 2021:
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

#ifndef GOBY_MIDDLEWARE_MARSHALLING_JSON_H
#define GOBY_MIDDLEWARE_MARSHALLING_JSON_H

#include <boost/type_index.hpp>

#include "goby/util/thirdparty/nlohmann/json.hpp"

#include "interface.h"

namespace goby
{
namespace middleware
{
/// \brief Support nlohmann JSON library in Goby3 using BSON encoding
template <> struct SerializerParserHelper<nlohmann::json, MarshallingScheme::JSON>
{
    static std::vector<char> serialize(const nlohmann::json& msg)
    {
        auto bson = nlohmann::json::to_bson(msg);
        std::vector<char> bytes(bson.begin(), bson.end());
        return bytes;
    }

    static std::string type_name(const nlohmann::json& d = nlohmann::json())
    {
        return "nlohmann::json";
    }

    template <typename CharIterator>
    static std::shared_ptr<nlohmann::json> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                                 CharIterator& actual_end,
                                                 const std::string& type = type_name())
    {
        actual_end = bytes_end;
        return std::make_shared<nlohmann::json>(nlohmann::json::from_bson(bytes_begin, bytes_end));
    }
};

template <typename T, class Enable = void> constexpr const char* json_type_name()
{
    return T::goby_json_type;
}

/// \brief Support arbitrary data types using nlohmann JSON (must define to_json/from_json functions for your data type: see nlohmann JSON docs)
template <typename T> struct SerializerParserHelper<T, MarshallingScheme::JSON>
{
    static std::vector<char> serialize(const T& msg)
    {
        nlohmann::json j = msg;
        return SerializerParserHelper<nlohmann::json, MarshallingScheme::JSON>::serialize(j);
    }

    static std::string type_name(const T& t = T()) { return json_type_name<T>(); }

    template <typename CharIterator>
    static std::shared_ptr<T> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                    CharIterator& actual_end, const std::string& type = type_name())
    {
        auto j = SerializerParserHelper<nlohmann::json, MarshallingScheme::JSON>::parse(
            bytes_begin, bytes_end, actual_end, type);

        return std::make_shared<T>(j->template get<T>());
    }
};

template <typename T,
          typename std::enable_if<std::is_same<T, nlohmann::json>::value>::type* = nullptr>
constexpr int scheme()
{
    return goby::middleware::MarshallingScheme::JSON;
}

template <typename T, typename std::enable_if<T::goby_json_type != nullptr>::type* = nullptr>
constexpr int scheme()
{
    return goby::middleware::MarshallingScheme::JSON;
}

} // namespace middleware
} // namespace goby

#endif
