// Copyright 2016-2021:
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

#ifndef GOBY_MIDDLEWARE_MARSHALLING_INTERFACE_H
#define GOBY_MIDDLEWARE_MARSHALLING_INTERFACE_H

#include <map>         // for map
#include <memory>      // for share...
#include <string>      // for string
#include <type_traits> // for is_void
#include <utility>     // for pair
#include <vector>      // for vector

#include "goby/middleware/marshalling/detail/primitive_type.h" // for primi...

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

    /// \brief Convert from a string to a marshalling scheme id.
    ///
    /// \param s string representation of marshalling scheme, e.g. "PROTOBUF" or "450"
    /// \return integer representation (e.g. 1 or 450).
    static int from_string(const std::string& s)
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

/// \brief Class for parsing and serializing a given marshalling scheme. Must be specialized for a particular scheme and/or DataType
///
/// \tparam DataType data type that the specialization can handle
/// \tparam scheme scheme that the specialization can handle
/// \tparam Enable SFINAE enable/disable type using type_traits for specializaing this struct
template <typename DataType, int scheme, class Enable = void> struct SerializerParserHelper
{
    /// \brief Given data, produce a vector of bytes
    static std::vector<char> serialize(const DataType& /*msg*/)
    {
        static_assert(std::is_void<Enable>::value, "SerializerParserHelper must be specialized");
        return std::vector<char>();
    }

    /// \brief The marshalling scheme specific string name for this type
    static std::string type_name()
    {
        static_assert(std::is_void<Enable>::value, "SerializerParserHelper must be specialized");
        return std::string();
    }

    /// \brief The marshalling scheme specific string name for this type, given a instantiation of the type (useful for specializations that can handle multiple types using runtime introspection)
    static std::string type_name(const DataType& /*d*/)
    {
        static_assert(std::is_void<Enable>::value, "SerializerParserHelper must be specialized");
        return std::string();
    }

    /// \brief Given a beginning and end iterator to bytes, parse the data and return it
    ///
    /// \tparam CharIterator an iterator to a container of bytes (char), e.g. std::vector<char>::iterator, or std::string::iterator
    /// \param bytes_begin Iterator to the beginning of a container of bytes
    /// \param bytes_end Iterator to the end of a container of bytes
    /// \param actual_end Will be set to the actual end of parsing by this specialization (useful for byte streams that are concatenated, e.g. DCCL)
    /// \param type Type name as string. If type_name() can be defined without parameters, the default value should be set, otherwise omit it (for dynamic types like google::protobuf::Message)
    /// \return shared pointer to the parsed message
    template <typename CharIterator>
    static std::shared_ptr<DataType> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                           CharIterator& actual_end,
                                           const std::string& type = type_name())
    {
        static_assert(std::is_void<Enable>::value, "SerializerParserHelper must be specialized");
        return std::shared_ptr<DataType>();
    }
};

//
// scheme
//

/// \brief Helper function for calling a particular transporter's scheme method
///
/// Different transporters may have different rules for determining allowable schemes or may enforce a different scheme for a particular data type. This function ensures the transporter has a chance to weigh in.
/// \tparam DataType data type to provide underlying marshalling scheme for
/// \tparam Transporter transporter type to determine
template <typename DataType, typename Transporter> constexpr int transporter_scheme()
{
    using detail::primitive_type;
    return Transporter::template scheme<typename primitive_type<DataType>::type>();
}

/// \brief Placeholder to provide an interface for the scheme() function family
///
/// Specializations must provide the numeric marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum for known schemes) given a DataType and perhaps a enable_if template parameter. These functions must be defined *before* they are used (due to C++ rules on constexpr functions), which typically means including the appropriate "marshalling" header(s) before include the relevant "transport" header(s). For example:
///
/// ```
/// // include Protobuf marshalling scheme definition (with scheme() function definition)
/// #include "goby/middleware/marshalling/protobuf.h"
///
/// // include InterProcessPortal definition (that uses the scheme() function in its template default parameters)
/// #include "goby/zeromq/transport/interprocess.h"
/// ```
///
///
/// \tparam DataType data type that the specialization can handle
template <typename DataType,
          typename std::enable_if<std::is_same<DataType, void>::value>::type* = nullptr>
constexpr int scheme()
{
    static_assert(std::is_same<DataType, void>::value, "Null scheme instantiated");
    return goby::middleware::MarshallingScheme::NULL_SCHEME;
}

} // namespace middleware
} // namespace goby

#endif
