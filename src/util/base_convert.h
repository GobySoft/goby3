// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include <gmp.h>

#include <string>

namespace goby
{
    namespace util
    {
        inline void base_convert(const std::string& source, std::string* sink, int source_base, int sink_base)
        {
            
            mpz_t base10;
            mpz_init(base10);

            // record number of most significant zeros, e.g. 0023 has two
            int ms_zeros = 0;
            // false until a non-zero byte is encountered
            bool non_zero_byte = false;
            
            for (int i = source.size()-1; i >= 0; --i)
            {
                int byte = 0xFF & source[i];
                mpz_add_ui(base10, base10, byte);
                if(i)
                    mpz_mul_ui(base10, base10, source_base);

                if(byte == 0 && !non_zero_byte) ++ms_zeros;
                else non_zero_byte = true;
            }            
            sink->clear();

            while(mpz_cmp_ui(base10, 0) != 0)
            {
                unsigned long int remainder =  mpz_fdiv_q_ui (base10, base10, sink_base);
                sink->push_back(0xFF & remainder);
            }

            // preserve MS zeros by adding that number to the most significant end
            for(int i = 0; i < ms_zeros; ++i)
                sink->push_back(0);
            
            mpz_clear(base10);
        }
    }
}
