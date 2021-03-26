// Copyright 2010-2021:
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

#ifndef GOBY_UTIL_BINARY_H
#define GOBY_UTIL_BINARY_H

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <bitset>
#include <boost/dynamic_bitset.hpp>

namespace goby
{
namespace util
{
/// \name Binary encoding
//@{

/// \brief Decodes a (little-endian) hexadecimal string to a byte string. Index 0 and 1 (first byte) of `in` are written to index 0 (first byte) of `out`
///
/// \param in hexadecimal string (e.g. "544f4d" or "544F4D")
/// \param out pointer to string to store result (e.g. "TOM").
inline void hex_decode(const std::string& in, std::string* out)
{
    static const short char0_9_to_number = 48;
    static const short charA_F_to_number = 55;
    static const short chara_f_to_number = 87;

    int in_size = in.size();
    int out_size = in_size >> 1;
    if (in_size & 1)
        ++out_size;

    out->assign(out_size, '\0');
    for (int i = (in_size & 1) ? -1 : 0, n = in_size; i < n; i += 2)
    {
        int out_i = (in_size & 1) ? (i + 1) / 2 : i / 2;

        if (i >= 0)
        {
            if (in[i] >= '0' && in[i] <= '9')
                (*out)[out_i] |= ((in[i] - char0_9_to_number) & 0x0f) << 4;
            else if (in[i] >= 'A' && in[i] <= 'F')
                (*out)[out_i] |= ((in[i] - charA_F_to_number) & 0x0f) << 4;
            else if (in[i] >= 'a' && in[i] <= 'f')
                (*out)[out_i] |= ((in[i] - chara_f_to_number) & 0x0f) << 4;
        }

        if (in[i + 1] >= '0' && in[i + 1] <= '9')
            (*out)[out_i] |= (in[i + 1] - char0_9_to_number) & 0x0f;
        else if (in[i + 1] >= 'A' && in[i + 1] <= 'F')
            (*out)[out_i] |= (in[i + 1] - charA_F_to_number) & 0x0f;
        else if (in[i + 1] >= 'a' && in[i + 1] <= 'f')
            (*out)[out_i] |= (in[i + 1] - chara_f_to_number) & 0x0f;
    }
}

inline std::string hex_decode(const std::string& in)
{
    std::string out;
    hex_decode(in, &out);
    return out;
}

/// \brief Encodes a (little-endian) hexadecimal string from a byte string. Index 0 of `in` is written to index 0 and 1 (first byte) of `out`
///
/// \param in byte string to encode (e.g. "TOM")
/// \param out pointer to string to store result (e.g. "544f4d")
/// \param upper_case set true to use upper case for the alphabet characters (i.e. A,B,C,D,E,F), otherwise lowercase is used (a,b,c,d,e,f).
inline void hex_encode(const std::string& in, std::string* out, bool upper_case = false)
{
    static const short char0_9_to_number = 48;
    static const short charA_F_to_number = 55;
    static const short chara_f_to_number = 87;

    int in_size = in.size();
    int out_size = in_size << 1;

    out->resize(out_size);
    for (int i = 0, n = in_size; i < n; ++i)
    {
        short msn = (in[i] >> 4) & 0x0f;
        short lsn = in[i] & 0x0f;

        if (msn >= 0 && msn <= 9)
            (*out)[2 * i] = msn + char0_9_to_number;
        else if (msn >= 10 && msn <= 15)
            (*out)[2 * i] = msn + (upper_case ? charA_F_to_number : chara_f_to_number);

        if (lsn >= 0 && lsn <= 9)
            (*out)[2 * i + 1] = lsn + char0_9_to_number;
        else if (lsn >= 10 && lsn <= 15)
            (*out)[2 * i + 1] = lsn + (upper_case ? charA_F_to_number : chara_f_to_number);
    }
}

inline std::string hex_encode(const std::string& in)
{
    std::string out;
    hex_encode(in, &out);
    return out;
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

//@}
} // namespace util
} // namespace goby

#endif
