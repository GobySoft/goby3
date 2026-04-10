// Copyright 2026:
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

// Tests PollerInterface::attach() by running one ZeroMQ portal and one
// UDPM portal on each fork, with the UDPM portal's poller attached to the ZeroMQ
// portal's poller.  Only the ZeroMQ portal's poll() is called on each fork;
// the UDPM portal is polled automatically via the attach mechanism.
//
// Process layout:
//   MANAGER_ROUTER – parent process: runs the ZMQ manager/router and waits for
//                    both child processes to complete.
//   FORK1 – first child:  ZeroMQ portal A + UDPM portal B.
//            B is attached to A.  Only A.poll() is ever called.
//            A+B publish to FORK2 and subscribe to receive from FORK2.
//   FORK2 – second child: ZeroMQ portal C + UDPM portal D.
//            D is attached to C.  Only C.poll() is ever called.
//            C+D publish to FORK1 and subscribe to receive from FORK1.
//
// Message flow:
//   A (ZMQ)  ──► C (ZMQ)   via group_a
//   B (UDPM) ──► D (UDPM)  via group_b
//   C (ZMQ)  ──► A (ZMQ)   via group_c
//   D (UDPM) ──► B (UDPM)  via group_d

#include <sys/types.h>
#include <sys/wait.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/transport/null.h"
#include "goby/udpm/transport/interprocess.h"
#include "goby/util/debug_logger.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/test/zeromq/poller_attach_test/test.pb.h"

#include <zmq.hpp>

using namespace goby::test::zeromq::poller_attach_test::protobuf;
using goby::glog;
using namespace goby::util::logger;

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
constexpr goby::middleware::Group group_a{"group_a"}; // A(ZMQ)  → C(ZMQ)
constexpr goby::middleware::Group group_b{"group_b"}; // B(UDPM) → D(UDPM)
constexpr goby::middleware::Group group_c{"group_c"}; // C(ZMQ)  → A(ZMQ)
constexpr goby::middleware::Group group_d{"group_d"}; // D(UDPM) → B(UDPM)

static const int n_msgs = 5;

// ---------------------------------------------------------------------------
// FORK1: ZMQ portal A + UDPM portal B, B attached to A, poll only A
// ---------------------------------------------------------------------------
void run_fork1(const goby::zeromq::protobuf::InterProcessPortalConfig& zmq_cfg,
               const goby::udpm::protobuf::InterProcessPortalConfig& udpm_cfg)
{
    // Shared NullTransporter gives A and B the same poll_mutex and cv, which
    // is required for attach().
    goby::middleware::NullTransporter null_inner;

    goby::zeromq::InterProcessPortal<goby::middleware::NullTransporter> portal_a(null_inner,
                                                                                 zmq_cfg);
    goby::udpm::InterProcessPortal<goby::middleware::NullTransporter> portal_b(null_inner,
                                                                               udpm_cfg);

    // Attach B to A: portal_a.poll() will also call _transporter_poll() on B.
    portal_a.attach(&portal_b);

    std::atomic<int> recv_a{0}; // messages A receives from C via ZMQ
    std::atomic<int> recv_b{0}; // messages B receives from D via UDPM

    portal_a.subscribe<group_c, Sample>(
        [&](const Sample& s)
        {
            glog.is(DEBUG1) && glog << "FORK1/A received from C (ZMQ): " << s.ShortDebugString()
                                    << std::endl;
            ++recv_a;
        });

    portal_b.subscribe<group_d, Sample>(
        [&](const Sample& s)
        {
            glog.is(DEBUG1) && glog << "FORK1/B received from D (UDPM): " << s.ShortDebugString()
                                    << std::endl;
            ++recv_b;
        });

    // Signal ZMQ manager that this client is ready.
    portal_a.ready();

    // Wait for ZMQ hold to be released (polls A, which also polls attached B).
    while (portal_a.hold_state()) portal_a.poll(std::chrono::milliseconds(100));

    // Extra settling time for UDPM multicast join.
    sleep(1);

    // Publish and poll interleaved so that we both send and receive on the
    // single polling thread.
    for (int i = 0; i < n_msgs; ++i)
    {
        Sample s;
        s.set_a(i);
        portal_a.publish<group_a>(s); // ZMQ → fork2's C
        portal_b.publish<group_b>(s); // UDPM → fork2's D

        // Poll A (also polls attached B).
        portal_a.poll(std::chrono::milliseconds(200));
    }

    // Drain any remaining incoming messages.
    auto timeout = std::chrono::system_clock::now() + std::chrono::seconds(30);
    while (recv_a < n_msgs || recv_b < n_msgs)
    {
        // Only A.poll() is called — B is polled automatically via attach.
        portal_a.poll(std::chrono::seconds(1));
        if (std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog << "FORK1 timed out: recv_a=" << recv_a << " recv_b=" << recv_b
                                 << " (expected " << n_msgs << " each)" << std::endl;
    }

    glog.is(VERBOSE) && glog << "FORK1 done: recv_a=" << recv_a << " recv_b=" << recv_b
                             << std::endl;
    assert(recv_a == n_msgs);
    assert(recv_b == n_msgs);
}

// ---------------------------------------------------------------------------
// FORK2: ZMQ portal C + UDPM portal D, D attached to C, poll only C
// ---------------------------------------------------------------------------
void run_fork2(const goby::zeromq::protobuf::InterProcessPortalConfig& zmq_cfg,
               const goby::udpm::protobuf::InterProcessPortalConfig& udpm_cfg)
{
    goby::middleware::NullTransporter null_inner;

    goby::zeromq::InterProcessPortal<goby::middleware::NullTransporter> portal_c(null_inner,
                                                                                 zmq_cfg);
    goby::udpm::InterProcessPortal<goby::middleware::NullTransporter> portal_d(null_inner,
                                                                               udpm_cfg);

    // Attach D to C: portal_c.poll() will also call _transporter_poll() on D.
    portal_c.attach(&portal_d);

    std::atomic<int> recv_c{0}; // messages C receives from A via ZMQ
    std::atomic<int> recv_d{0}; // messages D receives from B via UDPM

    portal_c.subscribe<group_a, Sample>(
        [&](const Sample& s)
        {
            glog.is(DEBUG1) && glog << "FORK2/C received from A (ZMQ): " << s.ShortDebugString()
                                    << std::endl;
            ++recv_c;
        });

    portal_d.subscribe<group_b, Sample>(
        [&](const Sample& s)
        {
            glog.is(DEBUG1) && glog << "FORK2/D received from B (UDPM): " << s.ShortDebugString()
                                    << std::endl;
            ++recv_d;
        });

    portal_c.ready();

    while (portal_c.hold_state()) portal_c.poll(std::chrono::milliseconds(100));

    sleep(1);

    for (int i = 0; i < n_msgs; ++i)
    {
        Sample s;
        s.set_a(i);
        portal_c.publish<group_c>(s); // ZMQ → fork1's A
        portal_d.publish<group_d>(s); // UDPM → fork1's B

        // Poll C (also polls attached D).
        portal_c.poll(std::chrono::milliseconds(200));
    }

    auto timeout = std::chrono::system_clock::now() + std::chrono::seconds(30);
    while (recv_c < n_msgs || recv_d < n_msgs)
    {
        // Only C.poll() is called — D is polled automatically via attach.
        portal_c.poll(std::chrono::seconds(1));
        if (std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog << "FORK2 timed out: recv_c=" << recv_c << " recv_d=" << recv_d
                                 << " (expected " << n_msgs << " each)" << std::endl;
    }

    glog.is(VERBOSE) && glog << "FORK2 done: recv_c=" << recv_c << " recv_d=" << recv_d
                             << std::endl;
    assert(recv_c == n_msgs);
    assert(recv_d == n_msgs);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int /*argc*/, char* argv[])
{
    // ---- ZMQ configuration ----
    goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
    zmq_cfg.set_platform("poller_attach_test");
    zmq_cfg.set_transport(goby::zeromq::protobuf::InterProcessPortalConfig::TCP);
    zmq_cfg.set_ip_address("127.0.0.1");
    zmq_cfg.set_tcp_port(54330);

    // ---- UDPM configuration (different address/port from defaults) ----
    goby::udpm::protobuf::InterProcessPortalConfig udpm_cfg;
    udpm_cfg.set_multicast_address("239.142.0.4");
    udpm_cfg.set_multicast_port(11147);

    // ---- Fork two child processes ----
    enum Role
    {
        MANAGER_ROUTER,
        FORK1,
        FORK2
    };
    Role role = MANAGER_ROUTER;

    if (fork() == 0)
        role = FORK1;
    else if (fork() == 0)
        role = FORK2;

    // ---- Set up logging ----
    const char* role_str = (role == MANAGER_ROUTER) ? "manager_router"
                           : (role == FORK1)        ? "fork1"
                                                    : "fork2";

    std::string log_path = std::string("/tmp/goby_test_poller_attach_") + role_str + ".log";
    std::ofstream log_file(log_path);
    goby::glog.add_stream(goby::util::logger::DEBUG3, &log_file);
    goby::glog.set_name(std::string(argv[0]) + "_" + role_str);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    switch (role)
    {
        case MANAGER_ROUTER:
        {
            // Run the ZMQ manager and router; hold until both clients connect.
            auto manager_context = std::make_unique<zmq::context_t>(1);
            auto router_context = std::make_unique<zmq::context_t>(10);

            goby::zeromq::protobuf::InterProcessManagerHold hold;
            hold.add_required_client("fork1");
            hold.add_required_client("fork2");

            goby::zeromq::Router router(*router_context, zmq_cfg);
            std::thread router_thread([&] { router.run(); });

            goby::zeromq::Manager manager(*manager_context, zmq_cfg, router, hold);
            std::thread manager_thread([&] { manager.run(); });

            // Wait for both child processes.
            int wstatus;
            for (int i = 0; i < 2; ++i)
            {
                wait(&wstatus);
                if (wstatus != 0)
                    exit(EXIT_FAILURE);
            }

            // Resetting contexts causes manager/router threads to return.
            router_context.reset();
            manager_context.reset();
            router_thread.join();
            manager_thread.join();
            break;
        }

        case FORK1:
        {
            auto fork1_zmq_cfg = zmq_cfg;
            fork1_zmq_cfg.set_client_name("fork1");
            run_fork1(fork1_zmq_cfg, udpm_cfg);
            break;
        }

        case FORK2:
        {
            auto fork2_zmq_cfg = zmq_cfg;
            fork2_zmq_cfg.set_client_name("fork2");
            run_fork2(fork2_zmq_cfg, udpm_cfg);
            break;
        }
    }

    glog.is(VERBOSE) && glog << role_str << ": all tests passed" << std::endl;
    std::cout << role_str << ": all tests passed" << std::endl;
}
