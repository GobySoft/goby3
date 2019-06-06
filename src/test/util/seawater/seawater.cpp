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
#include <boost/units/io.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include <iomanip>
#include <iostream>

#include <dccl/common.h>

#include "goby/util/seawater.h"

using namespace boost::units;

BOOST_AUTO_TEST_CASE(salinity_check_value)
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

BOOST_AUTO_TEST_CASE(soundspeed_check_value)
{
    auto test_temperature = 25.0 * absolute<celsius::temperature>();
    quantity<si::dimensionless> test_salinity = 35.0;
    auto test_depth = 1000.0 * si::meters;

    auto test_temperature_kelvin = (273.15 + 25.0) * absolute<si::temperature>();
    double test_salinity_dbl = 35.0;
    auto test_depth_km = 1.0 * si::kilo * si::meters;

    auto calculated_soundspeed =
        goby::util::mackenzie_soundspeed(test_temperature, test_salinity, test_depth);

    std::cout << "calculated speed of sound: " << std::fixed << std::setprecision(3)
              << calculated_soundspeed << " for T = " << test_temperature
              << ", S = " << test_salinity << ", and D = " << test_depth << std::endl;

    // check value for mackenzie
    BOOST_CHECK_CLOSE(static_cast<double>(calculated_soundspeed / si::meters_per_second), 1550.744,
                      std::pow(10, -3));

    // check using different input units
    BOOST_CHECK_CLOSE(
        static_cast<double>(goby::util::mackenzie_soundspeed(test_temperature_kelvin,
                                                             test_salinity_dbl, test_depth_km) /
                            si::meters_per_second),
        1550.744, std::pow(10, -3));
}

BOOST_AUTO_TEST_CASE(soundspeed_out_of_range)
{
    auto test_temperature = 25.0 * absolute<celsius::temperature>();
    quantity<si::dimensionless> test_salinity = 35.0;
    auto test_depth = 1000.0 * si::meters;

    auto out_of_range_temperature = 40.0 * absolute<si::temperature>();
    quantity<si::dimensionless> out_of_range_salinity = 0.0;
    auto out_of_range_depth = 9.0 * si::kilo * si::meters;

    BOOST_CHECK_THROW(
        goby::util::mackenzie_soundspeed(out_of_range_temperature, test_salinity, test_depth),
        std::out_of_range);
    BOOST_CHECK_THROW(
        goby::util::mackenzie_soundspeed(test_temperature, out_of_range_salinity, test_depth),
        std::out_of_range);
    BOOST_CHECK_THROW(
        goby::util::mackenzie_soundspeed(test_temperature, test_salinity, out_of_range_depth),
        std::out_of_range);
}
