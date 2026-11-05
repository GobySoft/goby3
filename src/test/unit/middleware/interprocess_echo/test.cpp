// Copyright 2016-2026:
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

// Tests TransporterConfig::echo on the interprocess layer: a publication is only delivered back
// to the thread that published it when echo is set, whether that thread holds the portal or a
// forwarder, and whether it subscribed by type or by regex. Other threads in the process always
// receive it.

#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/transport/interthread.h"

#if defined(test_for_zeromq)
#include "goby/zeromq/transport/interprocess.h"
namespace impl = goby::zeromq;
#elif defined(test_for_udpm)
#include "goby/udpm/transport/interprocess.h"
namespace impl = goby::udpm;
#elif defined(test_for_zenoh)
#include "goby/zenoh/transport/interprocess.h"
namespace impl = goby::zenoh;
#endif

#include "goby/test/unit/middleware/interprocess_echo/test.pb.h"
#include "goby/util/debug_logger.h"

using goby::glog;
using goby::test::middleware::protobuf::Sample;
using namespace goby::util::logger;

constexpr goby::middleware::Group sample{"sample"};

enum Node
{
    PORTAL = 0,
    FORWARDER_A = 1,
    FORWARDER_B = 2,
    NUM_NODES = 3
};
const std::vector<std::string> node_names{"portal", "forwarder A", "forwarder B"};

// each node publishes this many messages with echo off, and this many with echo on
constexpr int publish_count = 5;

std::mutex counts_mutex;
// (receiver, source) -> messages received
std::map<std::pair<int, int>, int> typed_counts;
std::map<std::pair<int, int>, int> regex_counts;

std::atomic<int> subscribed{0};
std::atomic<bool> go{false};
std::atomic<bool> done{false};

void record(std::map<std::pair<int, int>, int>& counts, int receiver, const Sample& s)
{
    std::lock_guard<std::mutex> lock(counts_mutex);
    ++counts[{receiver, s.source()}];
    glog.is_debug1() && glog << node_names[receiver] << " received " << s.ShortDebugString()
                             << std::endl;
}

template <typename Transporter> void subscribe_all(Transporter& ipc, int node)
{
    ipc.template subscribe<sample, Sample>([node](const Sample& s)
                                           { record(typed_counts, node, s); });
    ipc.subscribe_regex(
        [node](const std::vector<unsigned char>& data, int /*scheme*/, const std::string& /*type*/,
               const goby::middleware::Group& /*group*/)
        {
            Sample s;
            s.ParseFromArray(data.data(), data.size());
            record(regex_counts, node, s);
        },
        {goby::middleware::MarshallingScheme::PROTOBUF}, ".*Sample", std::string(sample));
}

template <typename Transporter> void publish_all(Transporter& ipc, int node)
{
    for (int i = 0; i < publish_count; ++i)
    {
        Sample s;
        s.set_source(node);

        s.set_echo(false);
        ipc.template publish<sample>(s);

        s.set_echo(true);
        goby::middleware::protobuf::TransporterConfig cfg;
        cfg.set_echo(true);
        ipc.template publish<sample>(s, goby::middleware::Publisher<Sample>(cfg));
    }
}

void forwarder(int node)
{
    goby::middleware::InterThreadTransporter inner;
    impl::InterProcessForwarder<goby::middleware::InterThreadTransporter> ipc(inner);
    subscribe_all(ipc, node);
    ++subscribed;

    while (!go) ipc.poll(std::chrono::milliseconds(10));
    publish_all(ipc, node);
    while (!done) ipc.poll(std::chrono::milliseconds(10));
}

bool check(const std::string& name, std::map<std::pair<int, int>, int>& counts)
{
    bool ok = true;
    for (int receiver = 0; receiver < NUM_NODES; ++receiver)
    {
        for (int source = 0; source < NUM_NODES; ++source)
        {
            // a node only gets its own publications when echo is on
            int expected = (receiver == source) ? publish_count : 2 * publish_count;
            int actual = counts[{receiver, source}];
            std::cout << name << ": " << node_names[receiver] << " received " << actual << " of "
                      << expected << " from " << node_names[source]
                      << (actual == expected ? "" : " ** MISMATCH **") << std::endl;
            if (actual != expected)
                ok = false;
        }
    }
    return ok;
}

int main(int /*argc*/, char* argv[])
{
    std::ofstream os("/tmp/goby_test_interprocess_echo");
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    const std::string client_name{"echo"};

#if defined(test_for_zeromq)
    goby::zeromq::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("interprocess_echo");
    cfg.set_manager_timeout_seconds(5);
    cfg.set_client_name(client_name);

    goby::zeromq::protobuf::InterProcessManagerHold hold;
    hold.add_required_client(client_name);
    zmq::context_t manager_context(1);
    zmq::context_t router_context(1);
    // like gobyd, the Manager shares the portal's thread
    goby::zeromq::Router router(router_context, cfg);
    std::thread router_thread([&] { router.run(); });
    goby::zeromq::Manager manager(manager_context, cfg, router, hold);
    std::thread manager_thread([&] { manager.run(); });
#elif defined(test_for_udpm)
    goby::udpm::protobuf::InterProcessPortalConfig cfg;
#elif defined(test_for_zenoh)
    goby::zenoh::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("interprocess_echo");
    cfg.add_listen_endpoint("tcp/127.0.0.1:0");
    cfg.set_client_name(client_name);
    cfg.mutable_hold()->add_required_client(client_name);
#endif

    {
        goby::middleware::InterThreadTransporter inner;
        impl::InterProcessPortal<goby::middleware::InterThreadTransporter> portal(inner, cfg);
        subscribe_all(portal, PORTAL);

        std::thread a(forwarder, FORWARDER_A);
        std::thread b(forwarder, FORWARDER_B);
        while (subscribed < 2) portal.poll(std::chrono::milliseconds(10));

#if defined(test_for_zeromq) || defined(test_for_zenoh)
        portal.ready();
        while (portal.hold_state()) portal.poll(std::chrono::milliseconds(10));
#endif
        // let the subscriptions settle
        auto settle_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < settle_until)
            portal.poll(std::chrono::milliseconds(10));

        go = true;
        publish_all(portal, PORTAL);

        auto receive_until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < receive_until)
            portal.poll(std::chrono::milliseconds(10));

        done = true;
        a.join();
        b.join();
    }

#if defined(test_for_zeromq)
    manager_context.close();
    router_context.close();
    router_thread.join();
    manager_thread.join();
#endif

    bool ok = check("typed", typed_counts);
    ok = check("regex", regex_counts) && ok;
    assert(ok);
    std::cout << "all tests passed" << std::endl;
}
