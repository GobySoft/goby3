// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

// Tests that the zeromq::InterProcessPortal keeps its hold until the subscriptions of every
// required client have reached its publish socket, rather than for a fixed delay after the
// Manager releases the hold (issue #188).
//
// Subscriptions reach a publisher's socket (SUB -> Router XPUB -> Router XSUB -> XPUB) by a
// different path than the client's "ready" report reaches the Manager, so the Manager's
// "hold: false" alone does not mean a required client's subscriptions have arrived at this
// process. Each client therefore subscribes a "ready marker" after all of its other
// subscriptions, and a publisher releases its hold only once it has seen the marker of every
// required client.
//
// The "publisher" and "subscriber" portals run in this process against an in-process Router and
// Manager, as gobyd runs them. A third required client ("detached") is driven with raw ZMQ
// sockets so the test controls the two halves of its startup separately: reporting ready to the
// Manager over the REQ socket, and subscribing its ready marker over a SUB socket.

#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/test/unit/zeromq/zeromq_hold_subscription_sync/test.pb.h"
#include "goby/util/debug_logger.h"

#include <zmq.hpp>

using namespace goby::test::zeromq::protobuf;
using goby::glog;
using namespace goby::util::logger;
namespace zpb = goby::zeromq::protobuf;

extern constexpr goby::middleware::Group sample{"Sample"};
constexpr int number_samples = 10;

using Portal = goby::zeromq::InterProcessPortal<>;
using Clock = std::chrono::steady_clock;

#ifdef USE_OLD_ZMQ_CPP_API
const int zmq_send_flags_none{0};
const int zmq_recv_flags_none{0};
#else
const auto zmq_send_flags_none{zmq::send_flags::none};
const auto zmq_recv_flags_none{zmq::recv_flags::none};
#endif

void zmq_set_linger_zero(zmq::socket_t& socket)
{
#ifdef USE_OLD_CPPZMQ_SETSOCKOPT
    socket.setsockopt(ZMQ_LINGER, 0);
#else
    socket.set(zmq::sockopt::linger, 0);
#endif
}

std::string socket_base(const zpb::InterProcessPortalConfig& cfg)
{
    return cfg.has_socket_name() ? cfg.socket_name() : "/tmp/goby_" + cfg.platform();
}

// Router and Manager threads as gobyd runs them
struct Daemon
{
    Daemon(zpb::InterProcessPortalConfig cfg, zpb::InterProcessManagerHold hold)
        : cfg_(std::move(cfg)),
          hold_(std::move(hold)),
          router_context_(std::make_unique<zmq::context_t>(1)),
          manager_context_(std::make_unique<zmq::context_t>(1)),
          router_(*router_context_, cfg_),
          router_thread_([this] { router_.run(); }),
          manager_(*manager_context_, cfg_, router_, hold_),
          manager_thread_([this] { manager_.run(); })
    {
    }

    ~Daemon()
    {
        manager_context_.reset();
        router_context_.reset();
        manager_thread_.join();
        router_thread_.join();
    }

    zpb::InterProcessPortalConfig cfg_;
    zpb::InterProcessManagerHold hold_;
    std::unique_ptr<zmq::context_t> router_context_;
    std::unique_ptr<zmq::context_t> manager_context_;
    goby::zeromq::Router router_;
    std::thread router_thread_;
    goby::zeromq::Manager manager_;
    std::thread manager_thread_;
};

// A required client without a portal: the test decides when it reports ready to the Manager and,
// separately, when its ready marker subscription reaches the Router
struct DetachedClient
{
    DetachedClient(const zpb::InterProcessPortalConfig& cfg, std::string name)
        : name_(std::move(name)), context_(1), req_(context_, ZMQ_REQ), sub_(context_, ZMQ_SUB)
    {
        zmq_set_linger_zero(req_);
        zmq_set_linger_zero(sub_);
        req_.connect(("ipc://" + socket_base(cfg) + ".manager").c_str());
        sub_.connect(("ipc://" + socket_base(cfg) + ".xpub").c_str());
    }

    // returns the Manager's hold state after this client reports ready
    bool report_ready()
    {
        zpb::ManagerRequest req;
        req.set_request(zpb::PROVIDE_HOLD_STATE);
        req.set_client_name(name_);
        req.set_client_pid(getpid());
        req.set_ready(true);

        zmq::message_t msg(req.ByteSizeLong());
        req.SerializeToArray(msg.data(), msg.size());
        req_.send(msg, zmq_send_flags_none);

        zmq::message_t reply;
#ifdef USE_OLD_ZMQ_CPP_API
        bool received = req_.recv(&reply, zmq_recv_flags_none);
#else
        bool received = bool(req_.recv(reply, zmq_recv_flags_none));
#endif
        assert(received);
        zpb::ManagerResponse response;
        response.ParseFromArray(reply.data(), reply.size());
        glog.is_debug1() && glog << name_ << " reported ready; manager replied: "
                                 << response.ShortDebugString() << std::endl;
        return response.hold();
    }

    void subscribe_ready_marker()
    {
        std::string marker = goby::zeromq::ready_marker_identifier(name_);
#ifdef USE_OLD_CPPZMQ_SETSOCKOPT
        sub_.setsockopt(ZMQ_SUBSCRIBE, marker.c_str(), marker.size());
#else
        sub_.set(zmq::sockopt::subscribe, marker);
#endif
        glog.is_debug1() && glog << name_ << " subscribed its ready marker" << std::endl;
    }

    std::string name_;
    zmq::context_t context_;
    zmq::socket_t req_;
    zmq::socket_t sub_;
};

// polls the portal until pred() holds or the timeout passes; returns pred()
template <typename Pred>
bool poll_until(Portal& portal, Pred pred, std::chrono::milliseconds timeout)
{
    auto deadline = Clock::now() + timeout;
    while (!pred() && Clock::now() < deadline) portal.poll(std::chrono::milliseconds(10));
    return pred();
}

// polls the portal for the given duration, returning false as soon as invariant() fails
template <typename Pred>
bool poll_while(Portal& portal, Pred invariant, std::chrono::milliseconds duration)
{
    auto deadline = Clock::now() + duration;
    while (Clock::now() < deadline)
    {
        if (!invariant())
            return false;
        portal.poll(std::chrono::milliseconds(10));
    }
    return invariant();
}

zpb::InterProcessPortalConfig client_cfg(const zpb::InterProcessPortalConfig& cfg,
                                         const std::string& name)
{
    auto client = cfg;
    client.set_client_name(name);
    return client;
}

// The hold stays on after the Manager releases it until the subscriptions of every required
// client have reached the publisher, and publications buffered during the hold then arrive.
void test_hold_waits_for_subscriptions()
{
    glog.is_verbose() && glog << "=== hold waits for required clients' subscriptions ==="
                              << std::endl;

    zpb::InterProcessPortalConfig cfg;
    cfg.set_platform("test_hold_sync");

    zpb::InterProcessManagerHold hold;
    hold.add_required_client("publisher");
    hold.add_required_client("subscriber");
    hold.add_required_client("detached");

    Daemon daemon(cfg, hold);

    std::atomic<int> received{0};
    std::atomic<bool> run_subscriber{true};
    std::thread subscriber_thread(
        [&]()
        {
            Portal subscriber(client_cfg(cfg, "subscriber"));
            subscriber.subscribe<sample, Sample>(
                [&received](const Sample& s)
                {
                    assert(s.a() == received);
                    ++received;
                });
            subscriber.ready();
            while (run_subscriber) subscriber.poll(std::chrono::milliseconds(10));
        });

    {
        Portal publisher(client_cfg(cfg, "publisher"));

        // buffered until the hold is released
        for (int i = 0; i < number_samples; ++i)
        {
            Sample s;
            s.set_a(i);
            publisher.publish<sample>(s);
        }
        publisher.ready();

        // long enough for publisher and subscriber to report ready and for their ready markers to
        // propagate, so that only "detached" keeps the hold on
        assert(poll_while(
            publisher, [&]() { return publisher.hold_state() && received == 0; },
            std::chrono::seconds(1)));

        DetachedClient detached(cfg, "detached");

        // the Manager now has every required client's ready report...
        bool manager_hold = detached.report_ready();
        assert(!manager_hold);

        // ... but "detached" has not subscribed anything yet, so nothing may be published
        assert(poll_while(
            publisher, [&]() { return publisher.hold_state() && received == 0; },
            std::chrono::seconds(1)));
        glog.is_verbose() && glog << "hold kept on after manager released it: ok" << std::endl;

        detached.subscribe_ready_marker();

        assert(poll_until(
            publisher, [&]() { return !publisher.hold_state(); }, std::chrono::seconds(5)));
        glog.is_verbose() && glog << "hold released once the marker arrived: ok" << std::endl;

        assert(poll_until(
            publisher, [&]() { return received == number_samples; }, std::chrono::seconds(5)));
        glog.is_verbose() && glog << "buffered publications received: ok" << std::endl;
    }

    run_subscriber = false;
    subscriber_thread.join();
}

// A required client that reported ready but whose subscriptions never arrive (e.g. it exited)
// must not block publishing forever: the hold is released after hold_release_timeout_seconds.
void test_hold_release_timeout()
{
    glog.is_verbose() && glog << "=== hold releases after timeout ===" << std::endl;

    zpb::InterProcessPortalConfig cfg;
    cfg.set_platform("test_hold_sync_timeout");
    cfg.set_hold_release_timeout_seconds(2);

    zpb::InterProcessManagerHold hold;
    hold.add_required_client("publisher");
    hold.add_required_client("ghost");

    Daemon daemon(cfg, hold);

    Portal publisher(client_cfg(cfg, "publisher"));
    publisher.ready();
    assert(poll_while(
        publisher, [&]() { return publisher.hold_state(); }, std::chrono::milliseconds(500)));

    DetachedClient ghost(cfg, "ghost");
    auto manager_released = Clock::now();
    bool manager_hold = ghost.report_ready();
    assert(!manager_hold);

    assert(
        poll_while(publisher, [&]() { return publisher.hold_state(); }, std::chrono::seconds(1)));
    assert(
        poll_until(publisher, [&]() { return !publisher.hold_state(); }, std::chrono::seconds(5)));

    auto held_for =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - manager_released);
    glog.is_verbose() && glog << "hold released after " << held_for.count() << " ms" << std::endl;
    assert(held_for >= std::chrono::milliseconds(1900));
}

int main(int /*argc*/, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::WARN, &std::cerr);
    std::ofstream os("/tmp/goby_test_zeromq_hold_subscription_sync");
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    test_hold_waits_for_subscriptions();
    test_hold_release_timeout();

    glog.is_verbose() && glog << "all tests passed" << std::endl;
    std::cout << "all tests passed" << std::endl;
}
