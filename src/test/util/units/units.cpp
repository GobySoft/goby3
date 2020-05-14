// Copyright 2019:
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

#define BOOST_TEST_MODULE units_test
#include <boost/test/included/unit_test.hpp>

#include <boost/units/io.hpp>
#include <boost/units/quantity.hpp>
#include <boost/units/systems/si/angular_velocity.hpp>
#include <boost/units/systems/si/frequency.hpp>

#include "goby/util/constants.h"
#include "goby/util/units/rpm/system.hpp"

bool close_enough(double a, double b, int precision)
{
    return std::abs(a - b) < std::pow(10, -precision);
}

int int_round(double a) { return std::round(a); }

using namespace boost::units;

BOOST_AUTO_TEST_CASE(rpm_system)
{
    //
    // Check RPM as a frequency (1 RPM == 1/60 Hz)
    //
    {
        quantity<goby::util::units::rpm::frequency> rpm_value1(1.0 * boost::units::si::hertz);
        std::cout << "RPM 1Hz: " << rpm_value1 << std::endl;
        // 1 Hz = 60 RPM
        BOOST_CHECK_EQUAL(int_round(rpm_value1.value()), 60);

        // 60 Hz = 3600 RPM
        quantity<boost::units::si::frequency> freq_value1(3600.0 * goby::util::units::rpm::rpms_f);
        std::cout << "Freq 3600RPM: " << freq_value1 << std::endl;
        BOOST_CHECK_EQUAL(int_round(freq_value1.value()), 60);
    }

    //
    // Check RPM as an angular velocity (1 RPM == 2*pi/60 rad/s)
    //
    {
        quantity<goby::util::units::rpm::angular_velocity> rpm_value1(
            2 * goby::util::pi<double> * boost::units::si::radians_per_second);
        std::cout << "2*pi rad/s: " << rpm_value1 << std::endl;
        // 2*pi rad/s = 60 RPM
        BOOST_CHECK_EQUAL(int_round(rpm_value1.value()), 60);

        // 3600 RPM = 2*pi*60 rad/s
        quantity<boost::units::si::angular_velocity> freq_value1(
            3600.0 * goby::util::units::rpm::rpms_omega);
        std::cout << "Ang velocity 3600RPM: " << freq_value1 << std::endl;
        BOOST_CHECK(close_enough(freq_value1.value(), 2.0 * goby::util::pi<double> * 60.0, 9));
    }
}
