// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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
#include "goby/util/constants.h"
#include <cassert>
#include <iostream>

using namespace goby::util;

bool double_cmp(double a, double b, int precision)
{
    int a_whole = a;
    int b_whole = b;

    int a_part = (a - a_whole) * pow(10.0, precision);
    int b_part = (b - b_whole) * pow(10.0, precision);

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

    std::map<double, double> table = {{0.0, 0}, {1.0, 300}, {1.1, 320}, {2.0, 500}};

    // exceeding bounds
    assert(std::round(linear_interpolate(-1.0, table)) == 0);
    assert(std::round(linear_interpolate(3.0, table)) == 500);

    // linear
    assert(std::round(linear_interpolate(0.0, table)) == 0);
    assert(std::round(linear_interpolate(1.0, table)) == 300);
    assert(std::round(linear_interpolate(1.1, table)) == 320);
    assert(std::round(linear_interpolate(1.2, table)) == 340);
    assert(std::round(linear_interpolate(1.3, table)) == 360);
    assert(std::round(linear_interpolate(1.4, table)) == 380);
    assert(std::round(linear_interpolate(1.5, table)) == 400);
    assert(std::round(linear_interpolate(1.6, table)) == 420);
    assert(std::round(linear_interpolate(1.7, table)) == 440);
    assert(std::round(linear_interpolate(1.8, table)) == 460);
    assert(std::round(linear_interpolate(1.9, table)) == 480);
    assert(std::round(linear_interpolate(2.0, table)) == 500);

    assert(std::isnan(goby::util::NaN<double>));
    assert(std::isnan(goby::util::NaN<float>));

    std::cout << "all tests passed" << std::endl;
    return 0;
}
