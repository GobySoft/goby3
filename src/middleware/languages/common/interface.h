// Copyright 2026:
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

#ifndef GOBY_MIDDLEWARE_LANGUAGES_COMMON_INTERFACE_H
#define GOBY_MIDDLEWARE_LANGUAGES_COMMON_INTERFACE_H

#include <cstdint> // for uint8_t
#include <ostream> // for ostream
#include <string>  // for string
#include <vector>  // for vector

#include "goby/middleware/marshalling/interface.h" // for MarshallingScheme, SerializerParserHelper

namespace goby
{
namespace middleware
{
/// \brief Support code shared by the Goby language bindings (e.g. Julia, Python)
///
/// The bindings all work the same way: the application author declares the publish/subscribe
/// interface statically in an interface.yml file, a generator turns that into C++ glue code, and
/// values cross the language boundary as marshalled bytes tagged with an Identifier. Everything
/// in this header is language-neutral; the language-specific pieces live alongside it in
/// goby/middleware/languages/<language>/application.h
namespace languages
{
/// \brief The publish/subscribe layer used for a given publication or subscription
///
/// These values are part of the interface between the C++ glue code and the bound language, so
/// they must not be renumbered.
enum class PubSubLayer
{
    INTERTHREAD = 0,
    INTERPROCESS = 1,
    INTERMODULE = 2
};

/// \brief Converts a PubSubLayer to the layer name used in interface.yml
inline std::string to_string(PubSubLayer layer)
{
    switch (layer)
    {
        case PubSubLayer::INTERTHREAD: return "interthread";
        case PubSubLayer::INTERPROCESS: return "interprocess";
        case PubSubLayer::INTERMODULE: return "intermodule";
    }
    return "unknown";
}

/// \brief Fully identifies a publication or subscription crossing the language boundary
///
/// This is the runtime equivalent of the compile-time (Group, Type, scheme) template parameters
/// used by the C++ transporters, plus the layer that would otherwise be chosen by calling
/// interthread(), interprocess() or intermodule().
struct Identifier
{
    PubSubLayer layer;
    std::string type_name;
    int scheme;
    std::string group;
};

inline bool operator==(const Identifier& i1, const Identifier& i2)
{
    return i1.layer == i2.layer && i1.type_name == i2.type_name && i1.scheme == i2.scheme &&
           i1.group == i2.group;
}

inline bool operator!=(const Identifier& i1, const Identifier& i2) { return !(i1 == i2); }

inline std::ostream& operator<<(std::ostream& os, const Identifier& i)
{
    return (os << "layer: " << to_string(i.layer) << ", type_name: \"" << i.type_name
               << "\", scheme: " << goby::middleware::MarshallingScheme::to_string(i.scheme)
               << ", group: \"" << i.group << "\"");
}

/// \brief Marshals a message using the given scheme, returning the bytes to hand to the bound language
template <typename DataType, int scheme>
std::vector<std::uint8_t> serialize_uint8(const DataType& msg)
{
    std::vector<char> out =
        goby::middleware::SerializerParserHelper<DataType, scheme>::serialize(msg);
    return std::vector<std::uint8_t>(out.begin(), out.end());
}

} // namespace languages
} // namespace middleware
} // namespace goby

#endif
