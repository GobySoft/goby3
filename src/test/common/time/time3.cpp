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

#include <cstdint>
#include <iomanip>
#include <iostream>

#include <boost/units/io.hpp>

#include "goby/common/time3.h"

using namespace boost::posix_time;
using namespace boost::gregorian;
using namespace goby::time;

// 2011-08-16 19:36:57.523456 UTC
const double TEST_DOUBLE_TIME = 1313523417.523456;
const std::uint64_t TEST_MICROSEC_TIME = TEST_DOUBLE_TIME * 1e6;
const boost::posix_time::ptime TEST_PTIME(date(2011, 8, 16),
                                          time_duration(19, 36, 57) + microseconds(523456));

bool double_cmp(double a, double b, double precision)
{
    return (std::abs(a - b) < std::pow(10.0, -precision));
}

int main()
{
    using boost::units::quantity;
    namespace si = boost::units::si;

    auto now_chrono = SystemClock::now();
    auto now_seconds = now<SITime>();
    auto now_microseconds = now<MicroTime>();

    static_assert(std::is_same<MicroTime::value_type, std::int64_t>(),
                  "expected int64_t value time");
    static_assert(std::is_same<SITime::value_type, double>(), "expected double value time");

    std::cout << "now (microseconds since epoch):\t"
              << std::setprecision(std::numeric_limits<double>::digits10)
              << now_chrono.time_since_epoch() / std::chrono::microseconds(1) << std::endl;
    std::cout << "now (seconds):\t\t\t" << std::setprecision(std::numeric_limits<double>::digits10)
              << now_seconds << std::endl;
    std::cout << "now (microseconds):\t\t" << now_microseconds << std::endl;

    std::cout << "seconds as microseconds:\t" << convert<decltype(now_microseconds)>(now_seconds)
              << std::endl;

    std::cout << "Time string: " << str() << std::endl;
    std::cout << "File string: " << file_str() << std::endl;

    // unsigned time
    quantity<MicroTimeUnit, std::uint64_t> unsigned_now_microseconds(now_microseconds);
    assert(now_microseconds == unsigned_now_microseconds);

    std::cout << "convert<SITime>(TEST_PTIME): " << convert<SITime>(TEST_PTIME) << std::endl;
    std::cout << "convert<MicroTime>(TEST_PTIME): " << convert<MicroTime>(TEST_PTIME) << std::endl;

    assert(convert<SystemClock::time_point>(TEST_PTIME).time_since_epoch() /
               std::chrono::microseconds(1) ==
           TEST_MICROSEC_TIME);
    assert(double_cmp(convert<SITime>(TEST_PTIME).value(), TEST_DOUBLE_TIME, 6));
    assert(convert<MicroTime>(TEST_PTIME).value() == TEST_MICROSEC_TIME);

    SimulatorSettings::warp_factor = 10;
    SimulatorSettings::using_sim_time = true;

    std::cout << "warp reference: " << convert<MicroTime>(SimulatorSettings::reference_time)
              << std::endl;
    auto ref_ptime = convert<boost::posix_time::ptime>(SimulatorSettings::reference_time);
    std::cout << "\tas ptime: " << ref_ptime << std::endl;

    assert(ref_ptime.date().day() == 1);
    assert(ref_ptime.date().month() == 1);
    assert(ref_ptime.date().year() ==
           boost::posix_time::second_clock::universal_time().date().year());

    auto now_warped_microseconds = now<MicroTime>();
    std::cout << "now (warped 10):\t\t" << now_warped_microseconds << std::endl;
    auto now_warped_ptime = convert<boost::posix_time::ptime>(now_warped_microseconds);

    std::cout << "\tas ptime: " << now_warped_ptime << std::endl;

    assert(now_warped_microseconds > now_microseconds);

    std::cout << "all tests passed" << std::endl;

    return 0;
}
