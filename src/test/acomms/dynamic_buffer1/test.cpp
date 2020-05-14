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

#define BOOST_TEST_MODULE dynamic_buffer_test

#include <boost/test/included/unit_test.hpp>

#include "goby/acomms/buffer/dynamic_buffer.h"
#include "goby/time/io.h"

struct TestClock
{
    typedef std::chrono::microseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<TestClock> time_point;
    static const bool is_steady = true;

    static time_point now() noexcept { return sim_now_; }

    static void increment(duration dur) { sim_now_ += dur; }

  private:
    static time_point sim_now_;
};

TestClock::time_point TestClock::sim_now_{std::chrono::microseconds(0)};

inline std::ostream& operator<<(std::ostream& out, const TestClock::time_point& time)
{
    return (out << std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch())
                       .count()
                << " us");
}

bool close_enough(double a, double b, int precision)
{
    return std::abs(a - b) < std::pow(10, -precision);
}

struct GLogSetup
{
    GLogSetup()
    {
        goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
        goby::glog.set_name("test");
    }

    ~GLogSetup() {}
};

BOOST_GLOBAL_FIXTURE(GLogSetup);

BOOST_AUTO_TEST_CASE(check_single_configuration)
{
    {
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg1);
        BOOST_CHECK_MESSAGE(cfg1.SerializeAsString() == buffer.cfg().SerializeAsString(),
                            "Expected " << cfg1.ShortDebugString()
                                        << ", got: " << buffer.cfg().ShortDebugString());
    }

    {
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        cfg1.set_ack_required(false);
        cfg1.set_ttl(2000);
        cfg1.set_value_base(10);
        cfg1.set_max_queue(5);

        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg1);
        BOOST_CHECK_MESSAGE(cfg1.SerializeAsString() == buffer.cfg().SerializeAsString(),
                            "Expected " << cfg1.ShortDebugString()
                                        << ", got: " << buffer.cfg().ShortDebugString());
    }
}

BOOST_AUTO_TEST_CASE(check_multi_configuration)
{
    goby::acomms::protobuf::DynamicBufferConfig cfg1;
    cfg1.set_ack_required(false);
    cfg1.set_ttl(2000);
    cfg1.set_value_base(10);
    cfg1.set_max_queue(5);

    goby::acomms::protobuf::DynamicBufferConfig cfg2;
    cfg2.set_ack_required(true);
    cfg2.set_ttl(3000);
    cfg2.set_value_base(20);
    cfg2.set_max_queue(10);
    cfg2.set_newest_first(false);

    goby::acomms::protobuf::DynamicBufferConfig expected_cfg;
    expected_cfg.set_ack_required(true);
    expected_cfg.set_ttl(2500);
    expected_cfg.set_value_base(15);
    expected_cfg.set_max_queue(10);
    expected_cfg.set_newest_first(false);

    goby::acomms::DynamicSubBuffer<std::string> buffer({cfg1, cfg2});
    BOOST_CHECK_MESSAGE(expected_cfg.SerializeAsString() == buffer.cfg().SerializeAsString(),
                        "Expected " << expected_cfg.ShortDebugString()
                                    << ", got: " << buffer.cfg().ShortDebugString());
}

BOOST_AUTO_TEST_CASE(check_top_value)
{
    goby::acomms::protobuf::DynamicBufferConfig cfg;

    // should be priority value of 1.0 after 10 ms
    cfg.set_ttl(10);
    cfg.set_value_base(1000);

    goby::acomms::DynamicSubBuffer<std::string, TestClock> buffer(cfg);
    BOOST_CHECK_EQUAL(buffer.top_value().first, -std::numeric_limits<double>::infinity());

    buffer.push("foo");

    BOOST_CHECK(!buffer.empty());

    for (int i = 1, n = 3; i <= n; ++i)
    {
        // reset last access
        buffer.top();
        TestClock::increment(std::chrono::milliseconds(10 * i));
        double v;
        typename goby::acomms::DynamicSubBuffer<std::string, TestClock>::ValueResult result;
        std::tie(v, result) = buffer.top_value();
        BOOST_CHECK_MESSAGE(close_enough(v, i * 1.0, 0), "Expected " << i * 1.0 << ", got: " << v);
    }
}

BOOST_AUTO_TEST_CASE(check_order)
{
    {
        goby::acomms::protobuf::DynamicBufferConfig cfg;
        cfg.set_newest_first(true);
        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg);

        buffer.push("first");
        buffer.push("second");

        BOOST_CHECK_EQUAL(buffer.top().data, "second");
        buffer.pop();
        BOOST_CHECK_EQUAL(buffer.top().data, "first");
    }

    {
        goby::acomms::protobuf::DynamicBufferConfig cfg;
        cfg.set_newest_first(false);
        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg);

        buffer.push("first");
        buffer.push("second");

        BOOST_CHECK_EQUAL(buffer.top().data, "first");
        buffer.pop();
        BOOST_CHECK_EQUAL(buffer.top().data, "second");
    }
}

BOOST_AUTO_TEST_CASE(check_subbuffer_expire)
{
    for (bool newest_first : {false, true})
    {
        goby::acomms::protobuf::DynamicBufferConfig cfg;
        using boost::units::si::milli;
        using boost::units::si::seconds;

        cfg.set_ttl_with_units(10.0 * milli * seconds);
        cfg.set_newest_first(newest_first);

        goby::acomms::DynamicSubBuffer<std::string, TestClock> buffer(cfg);
        buffer.push("first");
        BOOST_CHECK_EQUAL(buffer.size(), 1);
        TestClock::increment(std::chrono::milliseconds(5));
        buffer.push("second");
        BOOST_CHECK_EQUAL(buffer.size(), 2);
        TestClock::increment(std::chrono::milliseconds(6));
        auto exp1 = buffer.expire();
        BOOST_CHECK_EQUAL(buffer.size(), 1);
        TestClock::increment(std::chrono::milliseconds(6));
        auto exp2 = buffer.expire();

        BOOST_CHECK(buffer.empty());
        BOOST_REQUIRE_EQUAL(exp1.size(), 1);
        BOOST_REQUIRE_EQUAL(exp1[0].data, "first");
        BOOST_REQUIRE_EQUAL(exp2.size(), 1);
        BOOST_REQUIRE_EQUAL(exp2[0].data, "second");
    }
}

struct DynamicBufferFixture
{
    DynamicBufferFixture()
    {
        using boost::units::si::milli;
        using boost::units::si::seconds;
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        cfg1.set_ack_required(false);
        cfg1.set_ttl_with_units(10.0 * milli * seconds);
        cfg1.set_value_base(10);
        cfg1.set_max_queue(2);
        cfg1.set_newest_first(true);
        buffer.create(goby::acomms::BROADCAST_ID, "A", cfg1);
        TestClock::increment(std::chrono::milliseconds(1));

        goby::acomms::protobuf::DynamicBufferConfig cfg2;
        cfg2.set_ack_required(true);
        cfg2.set_ttl_with_units(10.0 * milli * seconds);
        cfg2.set_value_base(10);
        cfg2.set_max_queue(2);
        cfg2.set_newest_first(false);
        buffer.create(goby::acomms::BROADCAST_ID, "B", cfg2);
    }

    ~DynamicBufferFixture() {}

    goby::acomms::DynamicBuffer<std::string, TestClock> buffer;
};

BOOST_FIXTURE_TEST_CASE(create_buffer, DynamicBufferFixture)
{
    BOOST_CHECK(buffer.empty());
    BOOST_CHECK_EQUAL(buffer.size(), 0);

    buffer.push({goby::acomms::BROADCAST_ID, "A", TestClock::now(), "first"});

    TestClock::increment(std::chrono::microseconds(1));
    goby::acomms::DynamicBuffer<std::string, TestClock>::Value vp = buffer.top();
    BOOST_CHECK_EQUAL(vp.modem_id, goby::acomms::BROADCAST_ID);
    BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
    BOOST_CHECK_EQUAL(vp.data, "first");

    BOOST_CHECK(buffer.erase(vp));
    BOOST_CHECK(buffer.empty());
}

BOOST_FIXTURE_TEST_CASE(two_subbuffer_contest, DynamicBufferFixture)
{
    auto now = TestClock::now();

    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "1"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "1"});
    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "2"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "2"});

    TestClock::increment(std::chrono::milliseconds(1));
    // will be "A" because it was created first (and last access is initialized to creation time)
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "2");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 3);
    }
    TestClock::increment(std::chrono::milliseconds(1));

    // now it will be "B"
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "B");
        BOOST_CHECK_EQUAL(vp.data, "1");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 2);
    }
    TestClock::increment(std::chrono::milliseconds(1));

    // A
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "1");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 1);
    }
    TestClock::increment(std::chrono::milliseconds(1));

    // B
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "B");
        BOOST_CHECK_EQUAL(vp.data, "2");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 0);
    }
}

BOOST_FIXTURE_TEST_CASE(arbitrary_erase, DynamicBufferFixture)
{
    auto now = TestClock::now();

    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "1"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "1"});
    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "2"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "2"});

    BOOST_CHECK_EQUAL(buffer.size(), 4);
    BOOST_CHECK(buffer.erase({goby::acomms::BROADCAST_ID, "A", now, "1"}));
    BOOST_CHECK_EQUAL(buffer.size(), 3);
    BOOST_CHECK(buffer.erase({goby::acomms::BROADCAST_ID, "A", now, "2"}));
    BOOST_CHECK_EQUAL(buffer.size(), 2);
    BOOST_CHECK(buffer.erase({goby::acomms::BROADCAST_ID, "B", now, "1"}));
    BOOST_CHECK_EQUAL(buffer.size(), 1);
    BOOST_CHECK(buffer.erase({goby::acomms::BROADCAST_ID, "B", now, "2"}));
    BOOST_CHECK_EQUAL(buffer.size(), 0);
}

BOOST_FIXTURE_TEST_CASE(check_expire, DynamicBufferFixture)
{
    auto now = TestClock::now();
    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "first"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "first"});
    BOOST_CHECK_EQUAL(buffer.size(), 2);
    buffer.push({goby::acomms::BROADCAST_ID, "A", now + std::chrono::milliseconds(5), "second"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now + std::chrono::milliseconds(5), "second"});
    BOOST_CHECK_EQUAL(buffer.size(), 4);
    TestClock::increment(std::chrono::milliseconds(11));
    auto exp1 = buffer.expire();
    BOOST_CHECK_EQUAL(buffer.size(), 2);
    TestClock::increment(std::chrono::milliseconds(6));
    auto exp2 = buffer.expire();

    BOOST_CHECK(buffer.empty());
    BOOST_REQUIRE_EQUAL(exp1.size(), 2);
    BOOST_REQUIRE_EQUAL(exp1[0].data, "first");
    BOOST_REQUIRE_EQUAL(exp1[1].data, "first");
    BOOST_REQUIRE_EQUAL(exp2.size(), 2);
    BOOST_REQUIRE_EQUAL(exp2[0].data, "second");
    BOOST_REQUIRE_EQUAL(exp2[1].data, "second");
}

BOOST_FIXTURE_TEST_CASE(check_max_queue, DynamicBufferFixture)
{
    auto now = TestClock::now();

    BOOST_CHECK_EQUAL(buffer.push({goby::acomms::BROADCAST_ID, "A", now, "1"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({goby::acomms::BROADCAST_ID, "A", now, "2"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({goby::acomms::BROADCAST_ID, "B", now, "1"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({goby::acomms::BROADCAST_ID, "B", now, "2"}).size(), 0);

    // newest first = true pushes out oldest
    {
        auto exceeded = buffer.push({goby::acomms::BROADCAST_ID, "A", now, "3"});
        BOOST_CHECK_EQUAL(exceeded.size(), 1);
        BOOST_CHECK_EQUAL(exceeded[0].subbuffer_id, "A");
        BOOST_CHECK_EQUAL(exceeded[0].push_time, now);
        BOOST_CHECK_EQUAL(exceeded[0].data, "1");
    }

    // newest first = false pushes out newest (value just pushed)
    {
        auto exceeded = buffer.push({goby::acomms::BROADCAST_ID, "B", now, "3"});

        // newest first pushes out oldest
        BOOST_CHECK_EQUAL(exceeded.size(), 1);
        BOOST_CHECK_EQUAL(exceeded[0].subbuffer_id, "B");
        BOOST_CHECK_EQUAL(exceeded[0].push_time, now);
        BOOST_CHECK_EQUAL(exceeded[0].data, "3");
    }
}

BOOST_FIXTURE_TEST_CASE(check_blackout_time, DynamicBufferFixture)
{
    auto now = TestClock::now();

    using boost::units::si::milli;
    using boost::units::si::seconds;
    goby::acomms::protobuf::DynamicBufferConfig cfg1;
    cfg1.set_ack_required(false);
    cfg1.set_ttl_with_units(10.0 * milli * seconds);
    cfg1.set_value_base(100);
    cfg1.set_blackout_time_with_units(10.0 * milli * seconds);
    cfg1.set_max_queue(2);
    cfg1.set_newest_first(true);
    buffer.replace(goby::acomms::BROADCAST_ID, "A", cfg1);

    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "1"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "1"});

    // would be A but it is in blackout
    {
        TestClock::increment(std::chrono::microseconds(1));
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "B");
        BOOST_CHECK_EQUAL(vp.data, "1");
    }
    TestClock::increment(std::chrono::milliseconds(10));
    // now it's A since we're not in blackout any more
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "1");
    }
}

BOOST_FIXTURE_TEST_CASE(check_size, DynamicBufferFixture)
{
    auto now = TestClock::now();

    goby::acomms::protobuf::DynamicBufferConfig cfg =
        buffer.sub(goby::acomms::BROADCAST_ID, "A").cfg();
    cfg.set_value_base(100);
    buffer.replace(goby::acomms::BROADCAST_ID, "A", cfg);

    buffer.push({goby::acomms::BROADCAST_ID, "A", now, "1234567890"});
    buffer.push({goby::acomms::BROADCAST_ID, "B", now, "1"});

    // would be A but it is too large
    {
        TestClock::increment(std::chrono::microseconds(1));
        auto vp = buffer.top(goby::acomms::BROADCAST_ID, 3);
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "B");
        BOOST_CHECK_EQUAL(vp.data, "1");
    }

    {
        auto vp = buffer.top(goby::acomms::BROADCAST_ID, 15);
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "1234567890");
    }
}

BOOST_FIXTURE_TEST_CASE(check_ack_timeout, DynamicBufferFixture)
{
    auto now = TestClock::now();

    BOOST_CHECK_EQUAL(buffer.push({goby::acomms::BROADCAST_ID, "A", now, "1"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({goby::acomms::BROADCAST_ID, "A", now, "2"}).size(), 0);

    TestClock::increment(std::chrono::milliseconds(1));
    int max_bytes = 100;
    {
        auto vp = buffer.top(goby::acomms::BROADCAST_ID, max_bytes, std::chrono::milliseconds(10));
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "2");
    }
    TestClock::increment(std::chrono::milliseconds(1));
    {
        auto vp = buffer.top(goby::acomms::BROADCAST_ID, max_bytes, std::chrono::milliseconds(10));
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "1");
    }

    {
        BOOST_CHECK_THROW(auto vp = buffer.top(goby::acomms::BROADCAST_ID, max_bytes,
                                               std::chrono::milliseconds(10)),
                          goby::acomms::DynamicBufferNoDataException);
    }
    TestClock::increment(std::chrono::milliseconds(10));
    {
        auto vp = buffer.top(goby::acomms::BROADCAST_ID, max_bytes, std::chrono::milliseconds(10));
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "2");
    }
}

namespace goby
{
namespace test
{
struct MultiIDDynamicBufferFixture
{
    MultiIDDynamicBufferFixture()
    {
        using boost::units::si::milli;
        using boost::units::si::seconds;
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        cfg1.set_ack_required(false);
        cfg1.set_ttl_with_units(10.0 * milli * seconds);
        cfg1.set_value_base(10);
        cfg1.set_max_queue(2);
        cfg1.set_newest_first(true);
        buffer.create(1, "A", cfg1);

        goby::acomms::protobuf::DynamicBufferConfig cfg2;
        cfg2.set_ack_required(true);
        cfg2.set_ttl_with_units(10.0 * milli * seconds);
        cfg2.set_value_base(10);
        cfg2.set_max_queue(2);
        cfg2.set_newest_first(false);
        buffer.create(2, "B", cfg2);
    }

    ~MultiIDDynamicBufferFixture() {}

    goby::acomms::DynamicBuffer<std::string, TestClock> buffer;
};
} // namespace test
} // namespace goby

BOOST_FIXTURE_TEST_CASE(two_destination_contest, goby::test::MultiIDDynamicBufferFixture)
{
    auto now = TestClock::now();

    buffer.push({1, "A", now, "1"});
    buffer.push({2, "B", now, "1"});
    buffer.push({1, "A", now, "2"});
    buffer.push({2, "B", now, "2"});

    TestClock::increment(std::chrono::milliseconds(1));
    // will be "A" because it was created first (and last access is initialized to creation time)
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "A");
        BOOST_CHECK_EQUAL(vp.data, "2");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 3);
    }

    TestClock::increment(std::chrono::milliseconds(1));
    // now it will be "B"
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "B");
        BOOST_CHECK_EQUAL(vp.data, "1");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 2);
    }

    TestClock::increment(std::chrono::milliseconds(1));
    // A, but we ask for dest 2, so B
    {
        auto vp = buffer.top(2);
        BOOST_CHECK_EQUAL(vp.subbuffer_id, "B");
        BOOST_CHECK_EQUAL(vp.data, "2");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 1);
    }
}
