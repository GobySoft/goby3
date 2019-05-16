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

#ifndef DCCLTransitionalConstants20091211H
#define DCCLTransitionalConstants20091211H

#include <bitset>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

//#include <boost/dynamic_bitset.hpp>

#include "goby/acomms/acomms_constants.h"
#include "goby/acomms/dccl.h"

namespace goby
{
namespace transitional
{
/// Enumeration of DCCL types used for sending messages. dccl_enum and dccl_string primarily map to cpp_string, dccl_bool to cpp_bool, dccl_int to cpp_long, dccl_float to cpp_double
enum DCCLType
{
    dccl_static, /*!<  \ref tag_static */
    dccl_bool,   /*!< \ref tag_bool */
    dccl_int,    /*!< \ref tag_int */
    dccl_float,  /*!< \ref tag_float */
    dccl_enum,   /*!< \ref tag_enum */
    dccl_string, /*!< \ref tag_string */
    dccl_hex     /*!< \ref tag_hex */
};
/// Enumeration of C++ types used in DCCL.
enum DCCLCppType
{
    cpp_notype, /*!< not one of the C++ types used in DCCL */
    cpp_bool,   /*!< C++ bool */
    cpp_string, /*!< C++ std::string */
    cpp_long,   /*!< C++ long */
    cpp_double  /*!< C++ double */
};

// 2^3 = 8
enum
{
    POWER2_BITS_IN_BYTE = 3
};
inline unsigned bits2bytes(unsigned bits) { return bits >> POWER2_BITS_IN_BYTE; }
inline unsigned bytes2bits(unsigned bytes) { return bytes << POWER2_BITS_IN_BYTE; }

// 2^1 = 2
enum
{
    POWER2_NIBS_IN_BYTE = 1
};
inline unsigned bytes2nibs(unsigned bytes) { return bytes << POWER2_NIBS_IN_BYTE; }
inline unsigned nibs2bytes(unsigned nibs) { return nibs >> POWER2_NIBS_IN_BYTE; }

inline std::string type_to_string(DCCLType type)
{
    switch (type)
    {
        case dccl_int: return "int";
        case dccl_hex: return "hex";
        case dccl_bool: return "bool";
        case dccl_string: return "string";
        case dccl_float: return "float";
        case dccl_static: return "static";
        case dccl_enum: return "enum";
    }
    return "notype";
}

inline std::string type_to_protobuf_type(DCCLType type)
{
    switch (type)
    {
        case dccl_int: return "int32";
        case dccl_hex: return "bytes";
        case dccl_bool: return "bool";
        case dccl_string: return "string";
        case dccl_float: return "double";
        case dccl_static: return "string";
        default:
            throw(goby::acomms::DCCLException(
                "cannot directly map DCCLv1 XML type to Protobuf Type"));
    }
    return "notype";
}

inline std::string type_to_string(DCCLCppType type)
{
    switch (type)
    {
        case cpp_long: return "long";
        case cpp_double: return "double";
        case cpp_string: return "string";
        case cpp_bool: return "bool";
        case cpp_notype: return "no_type";
    }
    return "notype";
}

const unsigned DCCL_NUM_HEADER_BYTES = 6;

const unsigned DCCL_NUM_HEADER_PARTS = 8;

enum DCCLHeaderPart
{
    HEAD_CCL_ID = 0,
    HEAD_DCCL_ID = 1,
    HEAD_TIME = 2,
    HEAD_SRC_ID = 3,
    HEAD_DEST_ID = 4,
    HEAD_MULTIMESSAGE_FLAG = 5,
    HEAD_BROADCAST_FLAG = 6,
    HEAD_UNUSED = 7
};

const std::string DCCL_HEADER_NAMES[] = {
    "_ccl_id",         "_id",     "_time", "_src_id", "_dest_id", "_multimessage_flag",
    "_broadcast_flag", "_unused",
};
inline std::string to_str(DCCLHeaderPart p) { return DCCL_HEADER_NAMES[p]; }

enum DCCLHeaderBits
{
    HEAD_CCL_ID_SIZE = 8,
    HEAD_DCCL_ID_SIZE = 9,
    HEAD_TIME_SIZE = 17,
    HEAD_SRC_ID_SIZE = 5,
    HEAD_DEST_ID_SIZE = 5,
    HEAD_FLAG_SIZE = 1,
    HEAD_UNUSED_SIZE = 2
};

/// \name Binary encoding
//@{

/// \brief converts a char (byte) array into a hex string
///
/// \param c pointer to array of char
/// \param s reference to string to put char into as hex
/// \param n length of c
/// the first two hex chars in s are the 0 index in c
inline bool char_array2hex_string(const unsigned char* c, std::string& s, const unsigned int n)
{
    std::stringstream ss;
    for (unsigned int i = 0; i < n; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(c[i]);

    s = ss.str();
    return true;
}

/// \brief turns a string of hex chars ABCDEF into a character array reading  each byte 0xAB,0xCD, 0xEF, etc.
inline bool hex_string2char_array(unsigned char* c, const std::string& s, const unsigned int n)
{
    for (unsigned int i = 0; i < n; i++)
    {
        std::stringstream ss;
        unsigned int in;
        ss << s.substr(2 * i, 2);
        ss >> std::hex >> in;
        c[i] = static_cast<unsigned char>(in);
    }
    return true;
}

/// \brief return a string represented the binary value of `l` for `bits` number of bits which reads MSB -> LSB
inline std::string long2binary_string(unsigned long l, unsigned short bits)
{
    char s[bits + 1];
    for (unsigned int i = 0; i < bits; i++)
    {
        s[bits - i - 1] = (l & 1) ? '1' : '0';
        l >>= 1;
    }
    s[bits] = '\0';
    return (std::string)s;
}

/// \brief converts a binary string ("1000101010101010") into a hex string ("8AAA")
inline std::string binary_string2hex_string(const std::string& bs)
{
    std::string hs;
    unsigned int bytes = (unsigned int)(std::ceil(bs.length() / 8.0));
    unsigned char c[bytes];

    for (size_t i = 0; i < bytes; ++i)
    {
        std::bitset<8> b(bs.substr(i * 8, 8));
        c[i] = (char)b.to_ulong();
    }

    char_array2hex_string(c, hs, bytes);

    return hs;
}


/// \brief converts a hex string ("8AAA") into a binary string ("1000101010101010")
///
/// only works on whole byte string (even number of nibbles)
inline std::string hex_string2binary_string(const std::string& bs)
{
    int bytes = bs.length() / 2;
    unsigned char c[bytes];

    hex_string2char_array(c, bs, bytes);

    std::string hs;

    for (size_t i = 0; i < (size_t)bytes; i++) hs += long2binary_string((unsigned long)c[i], 8);

    return hs;
}


/// \brief attempts to convert a hex string into a numerical representation (of type T)
///
/// \return true if conversion succeeds, false otherwise
template <typename T> bool hex_string2number(const std::string& s, T& t)
{
    std::stringstream ss;
    ss << s;
    ss >> std::hex >> t;
    return !ss.fail();
}

/// \brief converts a decimal number of type T into a hex string
///
/// \param s string reference to store result in
/// \param t decimal number to convert
/// \param width desired width (in characters) of return string. Width should be twice the number of bytes
/// \return true if successful, false otherwise
template <typename T> bool number2hex_string(std::string& s, const T& t, unsigned int width = 2)
{
    std::stringstream ss;
    ss << std::hex << std::setw(width) << std::setfill('0') << static_cast<unsigned int>(t);
    s = ss.str();
    return !ss.fail();
}

/// \brief converts a decimal number of type T into a hex string assuming success
///
/// \param t decimal number to convert
/// \param width desired width (in characters) of return string. Width should be twice the number of bytes
/// \return hex string
template <typename T> std::string number2hex_string(const T& t, unsigned int width = 2)
{
    std::string s;
    number2hex_string(s, t, width);
    return s;
}

} // namespace transitional
} // namespace goby
#endif
