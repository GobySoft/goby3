// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
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

#define BOOST_TEST_MODULE seawater_formulas_test
#include <boost/test/included/unit_test.hpp>
#include <boost/units/io.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include <iomanip>
#include <iostream>

#include <dccl/common.h>

#include "goby/util/constants.h"
#include "goby/util/seawater.h"

using namespace boost::units;

bool close_enough(double a, double b, int precision)
{
    return std::abs(a - b) < std::pow(10, -precision);
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
        goby::util::seawater::mackenzie_soundspeed(test_temperature, test_salinity, test_depth);

    auto expected_soundspeed = 1550.744 * si::meters_per_second;
    int expected_precision = 3;

    std::cout << "CHECK [speed of sound] expected: " << std::fixed
              << std::setprecision(expected_precision) << expected_soundspeed
              << ", calculated: " << calculated_soundspeed << " for T = " << test_temperature
              << ", S = " << test_salinity << ", and D = " << test_depth << std::endl;

    // check value for mackenzie
    BOOST_CHECK(close_enough(calculated_soundspeed / si::meters_per_second,
                             expected_soundspeed / si::meters_per_second, expected_precision));

    // check using different input units
    BOOST_CHECK(close_enough(goby::util::seawater::mackenzie_soundspeed(
                                 test_temperature_kelvin, test_salinity_dbl, test_depth_km) /
                                 si::meters_per_second,
                             expected_soundspeed / si::meters_per_second, expected_precision));
}

BOOST_AUTO_TEST_CASE(soundspeed_out_of_range)
{
    auto test_temperature = 25.0 * absolute<celsius::temperature>();
    quantity<si::dimensionless> test_salinity = 35.0;
    auto test_depth = 1000.0 * si::meters;

    auto out_of_range_temperature = 40.0 * absolute<si::temperature>();
    quantity<si::dimensionless> out_of_range_salinity = 0.0;
    auto out_of_range_depth = 9.0 * si::kilo * si::meters;

    BOOST_CHECK_THROW(goby::util::seawater::mackenzie_soundspeed(out_of_range_temperature,
                                                                 test_salinity, test_depth),
                      std::out_of_range);
    BOOST_CHECK_THROW(goby::util::seawater::mackenzie_soundspeed(test_temperature,
                                                                 out_of_range_salinity, test_depth),
                      std::out_of_range);
    BOOST_CHECK_THROW(goby::util::seawater::mackenzie_soundspeed(test_temperature, test_salinity,
                                                                 out_of_range_depth),
                      std::out_of_range);
}

BOOST_AUTO_TEST_CASE(depth_check_value)
{
    using boost::units::si::deci;
    using goby::util::seawater::bar;

    auto pressure = 10000.0 * deci * bar;
    auto latitude = 30.0 * degree::degrees;

    auto calculated_depth = goby::util::seawater::depth(pressure, latitude);
    auto expected_depth = 9712.653 * si::meters;
    int expected_precision = 3;

    std::cout << "CHECK [depth] expected: " << std::fixed << std::setprecision(expected_precision)
              << expected_depth << ", calculated: " << calculated_depth << " for P = " << pressure
              << ", Lat = " << latitude << std::endl;

    BOOST_CHECK(close_enough(calculated_depth / si::meters, expected_depth / si::meters,
                             expected_precision));
}

// check a few of the other values from the "Table of Depth" in UNESCO 1983
BOOST_AUTO_TEST_CASE(depth_additional_values)
{
    using boost::units::si::deci;
    using goby::util::seawater::bar;
    {
        auto pressure = 500.0 * deci * bar;
        auto latitude = 0.0 * degree::degrees;
        auto calculated_depth = goby::util::seawater::depth(pressure, latitude);
        auto expected_depth = 496.65 * si::meters;
        int expected_precision = 2;

        BOOST_CHECK(close_enough(calculated_depth / si::meters, expected_depth / si::meters,
                                 expected_precision));
    }
    {
        auto pressure = 500.0 * bar; // 5000 dbar
        auto latitude = 60.0 * degree::degrees;
        auto calculated_depth = goby::util::seawater::depth(pressure, latitude);
        auto expected_depth = 4895.60 * si::meters;
        int expected_precision = 2;

        BOOST_CHECK(close_enough(calculated_depth / si::meters, expected_depth / si::meters,
                                 expected_precision));
    }
    {
        auto pressure = 9000.0 * deci * bar;
        auto latitude = goby::util::pi<double> / 2 * si::radians; // 90 deg
        auto calculated_depth = goby::util::seawater::depth(pressure, latitude);
        auto expected_depth = 8724.85 * si::meters;
        int expected_precision = 2;

        BOOST_CHECK(close_enough(calculated_depth / si::meters, expected_depth / si::meters,
                                 expected_precision));
    }
}

BOOST_AUTO_TEST_CASE(pressure_check_value)
{
    using boost::units::si::deci;
    using goby::util::seawater::bar;

    auto depth = 7321.45 * si::meters;
    auto latitude = 30.0 * degree::degrees;

    auto calculated_pressure = goby::util::seawater::pressure(depth, latitude);
    auto expected_pressure = 7500.006 * deci * bar;
    int expected_precision = 3;

    std::cout << "CHECK [pressure] expected: " << std::fixed
              << std::setprecision(expected_precision) << expected_pressure
              << ", calculated: " << calculated_pressure << " for D = " << depth
              << ", Lat = " << latitude << std::endl;

    BOOST_CHECK(
        close_enough(calculated_pressure.value(), expected_pressure.value(), expected_precision));
}

BOOST_AUTO_TEST_CASE(salinity_check_value)
{
    using boost::units::si::deci;
    using goby::util::seawater::bar;
    // from UNESCO 1983 test cases
    auto test_conductivity_ratio = 1.888091;
    auto test_conductivity =
        test_conductivity_ratio * goby::util::seawater::conductivity_at_standard;

    auto test_temperature = 40.0 * absolute<celsius::temperature>();
    auto test_pressure = 10000.0 * deci * bar;

    // sanity check that dS/m == mS/cm
    BOOST_CHECK_EQUAL(1 * si::deci * si::conductivity(),
                      1 * goby::util::seawater::milli_siemens_per_cm);

    double calculated_salinity =
        goby::util::seawater::salinity(test_conductivity, test_temperature, test_pressure);

    double expected_salinity = 40.00000;
    int expected_precision = 5;

    std::cout << "CHECK [salinity] expected: " << std::fixed
              << std::setprecision(expected_precision) << expected_salinity
              << ", calculated: " << calculated_salinity << " for T = " << test_temperature
              << ", P = " << test_pressure << ", C = " << test_conductivity << std::endl;

    BOOST_CHECK(close_enough(calculated_salinity, expected_salinity, expected_precision));
}

BOOST_AUTO_TEST_CASE(conductivity_check_value)
{
    using boost::units::si::deci;
    using goby::util::seawater::bar;
    // from UNESCO 1983 test cases
    auto expected_conductivity_ratio = 1.888091;

    int expected_precision = 6;

    auto test_temperature = 40.0 * absolute<celsius::temperature>();
    auto test_pressure = 10000.0 * deci * bar;

    double test_salinity = 40.00000;

    auto calculated_conductivity =
        goby::util::seawater::conductivity(test_salinity, test_temperature, test_pressure);
    double calculated_conductivity_ratio =
        calculated_conductivity / goby::util::seawater::conductivity_at_standard;

    std::cout << "CHECK [conductivity] expected (ratio): " << std::fixed
              << std::setprecision(expected_precision) << expected_conductivity_ratio
              << ", calculated (ratio): " << calculated_conductivity_ratio
              << " for T = " << test_temperature << ", P = " << test_pressure
              << ", SAL = " << test_salinity << std::endl;

    BOOST_CHECK(close_enough(calculated_conductivity_ratio, expected_conductivity_ratio,
                             expected_precision));
}

BOOST_AUTO_TEST_CASE(density_anomaly_check_value)
{
    using boost::units::si::deci;
    using goby::util::seawater::bar;
    // from UNESCO 1983 test cases

    auto expected_density_anomaly = 59.82037 * si::kilograms_per_cubic_meter;
    int expected_precision = 5;

    auto test_temperature = 40.0 * absolute<celsius::temperature>();
    auto test_pressure = 10000.0 * deci * bar;
    double test_salinity = 40.0;

    auto calculated_density_anomaly =
        goby::util::seawater::density_anomaly(test_salinity, test_temperature, test_pressure);

    std::cout << "CHECK [density anomaly] expected: " << std::fixed
              << std::setprecision(expected_precision) << expected_density_anomaly
              << ", calculated: " << calculated_density_anomaly << " for T = " << test_temperature
              << ", P = " << test_pressure << ", SAL = " << test_salinity << std::endl;

    BOOST_CHECK(close_enough(calculated_density_anomaly / si::kilograms_per_cubic_meter,
                             expected_density_anomaly / si::kilograms_per_cubic_meter,
                             expected_precision));
}
