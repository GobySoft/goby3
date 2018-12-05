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

#include "goby/util/seawater/salinity.h"
#include "goby/util/sci.h"
#include <cassert>
#include <iomanip>
#include <iostream>

int main()
{
    using goby::util::unbiased_round;

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

    assert(goby::util::unbiased_round(calculated_salinity, 5) == 40.00000);
}
