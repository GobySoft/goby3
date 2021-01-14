// Copyright 2013-2020:
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

#include "base_convert.h"

#ifdef HAS_GMP
#include <boost/multiprecision/gmp.hpp> // for divide_qr
#else
#include <boost/multiprecision/cpp_int.hpp>
#endif

#include <boost/multiprecision/number.hpp>

void goby::util::base_convert(const std::string& source, std::string* sink, int source_base,
                              int sink_base)
{
    using namespace boost::multiprecision;

#ifdef HAS_GMP
    using Integer = mpz_int;
#else
    using Integer = cpp_int;
#endif

    Integer base10;
    Integer source_base_mp(source_base);
    Integer sink_base_mp(sink_base);

    // record number of most significant zeros, e.g. 0023 has two
    int ms_zeros = 0;
    // false until a non-zero byte is encountered
    bool non_zero_byte = false;

    for (int i = source.size() - 1; i >= 0; --i)
    {
        Integer byte(0xFF & source[i]);
        add(base10, base10, byte);
        if (i)
            multiply(base10, base10, source_base_mp);

        if (byte == 0 && !non_zero_byte)
            ++ms_zeros;
        else
            non_zero_byte = true;
    }
    sink->clear();

    while (base10 != 0)
    {
        Integer remainder;
        divide_qr(base10, sink_base_mp, base10, remainder);
        sink->push_back(0xFF & remainder.convert_to<unsigned long int>());
    }

    // preserve MS zeros by adding that number to the most significant end
    for (int i = 0; i < ms_zeros; ++i) sink->push_back(0);
}
