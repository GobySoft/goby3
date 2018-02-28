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

#include "goby/util/sci.h"
#include <cassert>
#include <iostream>

using namespace goby::util;

bool double_cmp(double a, double b, int precision)
{
    int a_whole = a;
    int b_whole = b;

    int a_part = (a-a_whole)*pow(10.0, precision);
    int b_part = (b-b_whole)*pow(10.0, precision);
    
    return (a_whole == b_whole) && (a_part == b_part);
}

int main()
{
    
    assert(ceil_log2(1023) == 10);
    assert(ceil_log2(1024) == 10);
    assert(ceil_log2(1025) == 11);

    assert(ceil_log2(15) == 4);
    assert(ceil_log2(16) == 4);
    assert(ceil_log2(17) == 5);

    assert(ceil_log2(328529398) == 29);

    assert(unbiased_round(5.5, 0) == 6);
    assert(unbiased_round(4.5, 0) == 4);

    assert(double_cmp(unbiased_round(4.123, 2), 4.12, 2));
    
    // check value for mackenzie
    assert(double_cmp(unbiased_round(mackenzie_soundspeed(25,35,1000),3), 1550.744, 3));
	   
    std::cout << "all tests passed" << std::endl;
    return 0;
}
