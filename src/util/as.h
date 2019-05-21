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

#ifndef STRING20100713H
#define STRING20100713H

#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/mpl/and.hpp>
#include <boost/mpl/logical.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/type_traits.hpp>
#include <boost/utility.hpp>

namespace goby
{
namespace util
{
/// \name goby::util::as, a "do-the-right-thing" type casting tool
//@{

template <typename To>
typename boost::enable_if<boost::is_arithmetic<To>, To>::type
_as_from_string(const std::string& from)
{
    try
    {
        return boost::lexical_cast<To>(from);
    }
    catch (boost::bad_lexical_cast&)
    {
        // return NaN or maximum value supported by the type
        return std::numeric_limits<To>::has_quiet_NaN ? std::numeric_limits<To>::quiet_NaN()
                                                      : std::numeric_limits<To>::max();
    }
}

// only works properly for enums with a defined 0 value
template <typename To>
typename boost::enable_if<boost::is_enum<To>, To>::type _as_from_string(const std::string& from)
{
    try
    {
        return static_cast<To>(boost::lexical_cast<int>(from));
    }
    catch (boost::bad_lexical_cast&)
    {
        return static_cast<To>(0);
    }
}

template <typename To>
typename boost::enable_if<boost::is_class<To>, To>::type _as_from_string(const std::string& from)
{
    try
    {
        return boost::lexical_cast<To>(from);
    }
    catch (boost::bad_lexical_cast&)
    {
        return To();
    }
}

template <> inline bool _as_from_string<bool>(const std::string& from)
{
    return (boost::iequals(from, "true") || boost::iequals(from, "1"));
}

template <> inline std::string _as_from_string<std::string>(const std::string& from)
{
    return from;
}

template <typename To, typename From> std::string _as_to_string(const From& from)
{
    try
    {
        return boost::lexical_cast<std::string>(from);
    }
    catch (boost::bad_lexical_cast&)
    {
        return std::string();
    }
}

/// specialization of as() for bool -> string
template <> inline std::string _as_to_string<std::string, bool>(const bool& from)
{
    return from ? "true" : "false";
}

template <> inline std::string _as_to_string<std::string, std::string>(const std::string& from)
{
    return from;
}

template <typename To, typename From>
typename boost::disable_if<boost::is_same<To, From>, To>::type _as_numeric(const From& from)
{
    try
    {
        return boost::numeric_cast<To>(from);
    }
    catch (boost::bad_numeric_cast&)
    {
        // return NaN or maximum value supported by the type
        return std::numeric_limits<To>::has_quiet_NaN ? std::numeric_limits<To>::quiet_NaN()
                                                      : std::numeric_limits<To>::max();
    }
}

template <typename To, typename From>
typename boost::enable_if<boost::is_same<To, From>, To>::type _as_numeric(const From& from)
{
    return from;
}

template <typename To> To as(const std::string& from) { return _as_from_string<To>(from); }

template <typename To, typename From>
typename boost::enable_if<boost::is_same<To, std::string>, To>::type as(const From& from)
{
    return _as_to_string<To, From>(from);
}

template <typename To, typename From>
typename boost::enable_if<boost::mpl::and_<boost::is_arithmetic<To>, boost::is_arithmetic<From> >,
                          To>::type
as(const From& from)
{
    return _as_numeric<To, From>(from);
}

// not much better we can do for enums than static cast them ...
template <typename To, typename From>
typename boost::enable_if<boost::mpl::and_<boost::is_enum<To>, boost::is_arithmetic<From> >,
                          To>::type
as(const From& from)
{
    return static_cast<To>(from);
}

enum FloatRepresentation
{
    FLOAT_DEFAULT,
    FLOAT_FIXED,
    FLOAT_SCIENTIFIC
};

template <typename To, typename From>
To as(const From& from, int precision, FloatRepresentation rep = FLOAT_DEFAULT)
{
    return as<To>(from);
}

template <>
inline std::string as<std::string, double>(const double& from, int precision,
                                           FloatRepresentation rep)
{
    std::stringstream out;
    switch (rep)
    {
        case FLOAT_DEFAULT: break;

        case FLOAT_FIXED: out << std::fixed; break;
        case FLOAT_SCIENTIFIC: out << std::scientific; break;
    }

    out << std::setprecision(precision) << from;
    return out.str();
}

template <>
inline std::string as<std::string, float>(const float& from, int precision, FloatRepresentation rep)
{
    std::stringstream out;
    switch (rep)
    {
        case FLOAT_DEFAULT: break;

        case FLOAT_FIXED: out << std::fixed; break;
        case FLOAT_SCIENTIFIC: out << std::scientific; break;
    }

    out << std::setprecision(precision) << from;
    return out.str();
}

} // namespace util
} // namespace goby
#endif
