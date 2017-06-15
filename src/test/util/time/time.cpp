// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "stdint.h"
#include "goby/common/time.h"

using goby::util::as;
using goby::uint64;
using goby::common::goby_time;
using namespace boost::posix_time;
using namespace boost::gregorian;

// 2011-08-16 19:36:57.523456 UTC
const double TEST_DOUBLE_TIME = 1313523417.523456;
const uint64 TEST_MICROSEC_TIME = TEST_DOUBLE_TIME * 1e6;
const boost::posix_time::ptime TEST_PTIME(date(2011,8,16),
                                          time_duration(19,36,57) +
                                          microseconds(523456));

bool double_cmp(double a, double b, int precision)
{
    long long a_whole = a;
    long long b_whole = b;

    int a_part = (a-a_whole)*pow(10.0, precision);
    int b_part = (b-b_whole)*pow(10.0, precision);
    
    return (a_whole == b_whole) && (a_part == b_part);
}

uint64 fake_time()
{
    return TEST_MICROSEC_TIME;
}


int main()
{
    std::cout << "current double: " << std::setprecision(16) << goby_time<double>() << std::endl;
    std::cout << "current uint64: " << goby_time<uint64>() << std::endl;
    std::cout << "current ptime: " << goby_time() << std::endl;


    std::cout << "double: " << std::setprecision(16) << TEST_DOUBLE_TIME << std::endl;
    std::cout << "uint64: " << TEST_MICROSEC_TIME << std::endl;
    std::cout << "ptime: " << TEST_PTIME << std::endl;

    assert(!double_cmp(5.4, 2.3, 1));
    
    
    assert(double_cmp(goby::common::ptime2unix_double(TEST_PTIME), TEST_DOUBLE_TIME, 6));
    assert(double_cmp(as<double>(TEST_PTIME), TEST_DOUBLE_TIME, 6)); // same as previous line
    
    std::cout << "goby::common::unix_double2ptime(TEST_DOUBLE_TIME) " << goby::common::unix_double2ptime(TEST_DOUBLE_TIME) << std::endl;
    
    assert(goby::common::unix_double2ptime(TEST_DOUBLE_TIME) == TEST_PTIME);
    assert(as<ptime>(TEST_DOUBLE_TIME) == TEST_PTIME);  // same as previous line

    std::cout << "goby::common::ptime2unix_microsec(TEST_PTIME) " << goby::common::ptime2unix_microsec(TEST_PTIME) << std::endl;

    assert(goby::common::ptime2unix_microsec(TEST_PTIME) == TEST_MICROSEC_TIME);
    assert(as<uint64>(TEST_PTIME) == TEST_MICROSEC_TIME);  // same as previous line    
    
    assert(goby::common::unix_microsec2ptime(TEST_MICROSEC_TIME) == TEST_PTIME);
    assert(as<ptime>(TEST_MICROSEC_TIME) == TEST_PTIME); // same as previous line

    std::cout << "as<string>(as<uint64>(ptime): " << goby::util::as<std::string>(goby::util::as<uint64>(TEST_PTIME)) << std::endl;
    
    
    assert(as<ptime>(std::numeric_limits<uint64>::max()) == ptime(not_a_date_time));

    std::cout << goby_time() << std::endl;
    assert(goby_time() - boost::posix_time::microsec_clock::universal_time() < boost::posix_time::milliseconds(10));
    std::cout << goby_time<double>() << std::endl;
    assert(as<ptime>(goby_time<double>()) - boost::posix_time::microsec_clock::universal_time() < boost::posix_time::milliseconds(10));
    std::cout << goby_time<uint64>() << std::endl;
    assert(as<ptime>(goby_time<uint64>()) - boost::posix_time::microsec_clock::universal_time() < boost::posix_time::milliseconds(10));

    goby::common::goby_time_function = &fake_time;
    
    std::cout << goby_time() << std::endl;
    std::cout << goby_time<double>() << std::endl;
    std::cout << goby_time<uint64>() << std::endl;
    assert(goby_time<uint64>() == TEST_MICROSEC_TIME); 
    assert(double_cmp(goby_time<double>(), TEST_DOUBLE_TIME, 6));
    assert(goby_time<ptime>() == TEST_PTIME); 
    

    const ptime FAR_FUTURE_COMPARISON_PTIME(date(2391,10,8),
                                            time_duration(9,50,9) +
                                            microseconds(399860));
    
    // test dates in the next century
    const double FAR_FUTURE_COMPARISON = 13309696209.39986;
    ptime far_future_ptime = goby::common::unix_double2ptime(FAR_FUTURE_COMPARISON);
    double far_future_time = goby::common::ptime2unix_double(far_future_ptime);
    std::cout << FAR_FUTURE_COMPARISON_PTIME << "=?" << far_future_ptime << "=?" << far_future_time << "=?" << FAR_FUTURE_COMPARISON << std::endl;
    
    assert(double_cmp(far_future_time,FAR_FUTURE_COMPARISON, 5));

    const uint64 FAR_FUTURE_COMPARISON_UINT64 = FAR_FUTURE_COMPARISON * 1e6;
    assert(goby::common::ptime2unix_microsec(goby::common::unix_microsec2ptime(FAR_FUTURE_COMPARISON_UINT64)) == FAR_FUTURE_COMPARISON_UINT64);

    
    std::cout << "all tests passed" << std::endl;
    
    return 0;
}
