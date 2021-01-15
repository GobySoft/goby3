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

#include "goby/util/debug_logger.h"
#include <cassert>

#include <sstream>

using goby::glog;

/// asserts false if called - used for testing proper short-circuiting of logger calls
inline std::ostream& stream_assert(std::ostream& /*os*/) { assert(false); }

using namespace goby::util::logger;

namespace goby
{
namespace test
{
namespace util
{
struct A
{
    A() = default;
    A(int i) : i(i) {}

    int i{0};
};

std::ostream& operator<<(std::ostream& out, const A& a) { return out << a.i; }

void spew(int n, int m, int run)
{
    switch (run)
    {
        case 0:
            glog.is(VERBOSE) && glog << "Spew 1: " << std::endl;
            for (int i = 0; i < n; i++) glog.is(VERBOSE) && glog << m << " " << i << std::endl;
            break;

        case 1:
            glog.is(VERBOSE) && glog << "Spew 2: " << std::endl;
            for (int i = 0; i < n; i++) glog.is(VERBOSE) && glog << m << " " << A(i) << std::endl;
            break;

        case 2:
            glog.is(VERBOSE) && glog << "Spew 3: " << std::endl;
            for (int i = 0; i < n; i++) glog << m << " " << i << std::endl;
            break;

        case 3:
            glog.is(VERBOSE) && glog << "Spew 4: " << std::endl;
            for (int i = 0; i < n; i++) glog << m << " " << i;
            glog << std::endl;
            break;

        case 4:
            glog.is(VERBOSE) && glog << "Spew 5: " << std::endl;
            for (int i = 0; i < n; i++)
                glog.is(VERBOSE) && glog << m << " " << i << std::endl << i << std::endl;
            break;
    }
}
} // namespace util
} // namespace test
} // namespace goby

using namespace goby::test::util;

int main()
{
    glog.set_name("test");

    std::cout << "attaching std::cout to QUIET" << std::endl;
    glog.add_stream(QUIET, &std::cout);
    glog.is(DEBUG3) && glog << stream_assert << std::endl;
    glog.is(DEBUG2) && glog << stream_assert << std::endl;
    glog.is(DEBUG1) && glog << stream_assert << std::endl;
    glog.is(VERBOSE) && glog << stream_assert << std::endl;
    glog.is(WARN) && glog << stream_assert << std::endl;

    std::cout << "attaching std::cout to WARN" << std::endl;
    glog.add_stream(WARN, &std::cout);
    glog.is(DEBUG3) && glog << stream_assert << std::endl;
    glog.is(DEBUG2) && glog << stream_assert << std::endl;
    glog.is(DEBUG1) && glog << stream_assert << std::endl;
    glog.is(VERBOSE) && glog << stream_assert << std::endl;
    glog.is(WARN) && glog << "warn ok" << std::endl;

    std::cout << "attaching std::cout to VERBOSE" << std::endl;
    glog.add_stream(VERBOSE, &std::cout);
    glog.is(DEBUG3) && glog << stream_assert << std::endl;
    glog.is(DEBUG2) && glog << stream_assert << std::endl;
    glog.is(DEBUG1) && glog << stream_assert << std::endl;
    glog.is(VERBOSE) && glog << "verbose ok" << std::endl;
    glog.is(WARN) && glog << "warn ok" << std::endl;

    std::cout << "checking locking ... " << std::endl;
    glog.set_lock_action(goby::util::logger_lock::lock);
    glog.is(VERBOSE) && glog << "lock ok" << std::endl;
    glog.is(VERBOSE) && glog << "unlock ok" << std::endl;
    // glog << "lock ok" << std::endl;
    // glog << std::string("lock ok") << std::endl;

    // A a;
    // a.i = 10;
    // glog << "Testing overloaded operator<<" << std::endl;
    // glog << a << std::endl;

    glog.is(DEBUG3) && glog << stream_assert << std::endl;

    for (int i = 0; i < 2; ++i)
    {
        std::thread t1([i]() { spew(1000, 1, i); });
        std::thread t2([i]() { spew(1000, 2, i); });
        t1.join();
        t2.join();
    }

    glog.set_lock_action(goby::util::logger_lock::none);

    std::cout << "attaching std::cout to DEBUG1" << std::endl;
    glog.add_stream(DEBUG1, &std::cout);
    glog.is(DEBUG3) && glog << stream_assert << std::endl;
    glog.is(DEBUG2) && glog << stream_assert << std::endl;
    glog.is(DEBUG1) && glog << "debug1 ok" << std::endl;
    glog.is(VERBOSE) && glog << "verbose ok" << std::endl;
    glog.is(WARN) && glog << "warn ok" << std::endl;

    std::cout << "attaching std::cout to DEBUG2" << std::endl;
    glog.add_stream(DEBUG2, &std::cout);
    glog.is(DEBUG3) && glog << stream_assert << std::endl;
    glog.is(DEBUG2) && glog << "debug2 ok" << std::endl;
    glog.is(DEBUG1) && glog << "debug1 ok" << std::endl;
    glog.is(VERBOSE) && glog << "verbose ok" << std::endl;
    glog.is(WARN) && glog << "warn ok" << std::endl;

    std::cout << "attaching std::cout to DEBUG3" << std::endl;
    glog.add_stream(DEBUG3, &std::cout);
    glog.is(DEBUG3) && glog << "debug3 ok" << std::endl;
    glog.is(DEBUG2) && glog << "debug2 ok" << std::endl;
    glog.is(DEBUG1) && glog << "debug1 ok" << std::endl;
    glog.is(VERBOSE) && glog << "verbose ok" << std::endl;
    glog.is(WARN) && glog << "warn ok" << std::endl;

    std::stringstream ss1;
    std::cout << "attaching stringstream to VERBOSE" << std::endl;
    glog.add_stream(VERBOSE, &ss1);
    glog.is(DEBUG3) && glog << "debug3 ok" << std::endl;
    glog.is(DEBUG2) && glog << "debug2 ok" << std::endl;
    glog.is(DEBUG1) && glog << "debug1 ok" << std::endl;
    glog.is(VERBOSE) && glog << "verbose ok" << std::endl;
    glog.is(WARN) && glog << "warn ok" << std::endl;

    std::cout << "ss1: \n" << ss1.rdbuf();

    glog.add_group("test1", goby::util::Colors::lt_green, "Test 1");
    glog.add_group("test2", goby::util::Colors::lt_green, "Test 2");

    glog << group("test1") << "test1 group ok" << std::endl;
    glog.is(WARN) && glog << group("test2") << "test2 group warning ok" << std::endl;

    std::cout << "All tests passed." << std::endl;
    return 0;
}
