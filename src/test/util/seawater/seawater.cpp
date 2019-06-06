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

#define BOOST_TEST_MODULE seawater - formulas - test
#include <boost/test/included/unit_test.hpp>

#include <iomanip>
#include <iostream>

#include <dccl/common.h>

#include "goby/util/seawater.h"

BOOST_AUTO_TEST_CASE(test_salinity)
{
    // from UNESCO 1983 test cases
    double test_conductivity_ratio = 1.888091;
    const double CONDUCTIVITY_AT_STANDARD = 42.914; // S = 35, T = 15 deg C, P = 0 dbar
    double test_conductivity_mSiemens_cm = test_conductivity_ratio * CONDUCTIVITY_AT_STANDARD;

    double test_temperature_deg_C = 40;
    double test_pressure_dbar = 10000;

    double calculated_salinity = SalinityCalculator::salinity(
        test_conductivity_mSiemens_cm, test_temperature_deg_C, test_pressure_dbar);

    std::cout << "calculated salinity: " << std::fixed << std::setprecision(5)
              << calculated_salinity << " for T = " << test_temperature_deg_C << " deg C,"
              << " P = " << test_pressure_dbar << " dbar,"
              << " C = " << test_conductivity_mSiemens_cm << " mSiemens / cm" << std::endl;

    BOOST_CHECK_CLOSE(calculated_salinity, 40.00000, std::pow(10, -5));
}

BOOST_AUTO_TEST_CASE(test_soundspeed)
{
    double test_temperature = 25;
    double test_salinity = 35;
    double test_depth = 1000;

    double calculated_soundspeed =
        goby::util::mackenzie_soundspeed(test_temperature, test_salinity, test_depth);

    std::cout << "calculated speed of sound: " << std::fixed << std::setprecision(3)
              << calculated_soundspeed << " m/s "
              << " for T = " << test_temperature << ", S = " << test_salinity
              << ", and D = " << test_depth << std::endl;

    // check value for mackenzie
    BOOST_CHECK_CLOSE(calculated_soundspeed, 1550.744, std::pow(10, -3));
}
