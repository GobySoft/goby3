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
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef SerializeParse20160607H
#define SerializeParse20160607H

#include <map>
#include <mutex>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include "goby/middleware/detail/primitive_type.h"

namespace goby
{
namespace middleware
{
//
// MarshallingScheme
//

/// \brief Enumeration and helper functions for marshalling scheme identification
struct MarshallingScheme
{
    /// \brief Marshalilng schemes implemented in Goby
    enum MarshallingSchemeEnum
    {
        ALL_SCHEMES = -2,
        NULL_SCHEME = -1,
        CSTR = 0,
        PROTOBUF = 1,
        DCCL = 2,
        //        CAPTN_PROTO = 3,
        //        MSGPACK = 4,
        CXX_OBJECT = 5,
        MAVLINK = 6
    };

    /// \brief Convert a known marshalling scheme to a human-readable string or an unknown scheme to the string representation of its numeric value
    ///
    /// \param e Marshalling scheme id to convert
    /// \return Marshalling scheme as string (e.g. "PROTOBUF" if e == 1 or "450" if e == 450)
    static std::string to_string(int e)
    {
        auto it = e2s.find(e);
        return it != e2s.end() ? it->second : std::to_string(e);
    }

    static int from_string(std::string s)
    {
        auto it = s2e.find(s);
        return it != s2e.end() ? it->second : std::stoi(s);
    }

  private:
    static const std::map<int, std::string> e2s;
    static const std::map<std::string, int> s2e;
};

//
// SerializerParserHelper
//

template <typename DataType, int scheme, class Enable = void> struct SerializerParserHelper
{
};

//
// scheme
//
template <typename T, typename Transporter> constexpr int transporter_scheme()
{
    using detail::primitive_type;
    return Transporter::template scheme<typename primitive_type<T>::type>();
}

// placeholder to provide an interface for the scheme() function family
template <typename T, typename std::enable_if<std::is_same<T, void>::value>::type* = nullptr>
constexpr int scheme()
{
    static_assert(std::is_same<T, void>::value, "Null scheme instantiated");
    return goby::middleware::MarshallingScheme::NULL_SCHEME;
}

} // namespace middleware
} // namespace goby

#endif
