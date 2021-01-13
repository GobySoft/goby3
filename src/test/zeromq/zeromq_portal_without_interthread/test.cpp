// Copyright 2016-2020:
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

#include <sys/types.h>
#include <sys/wait.h>

#include <atomic>
#include <deque>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/test/zeromq/zeromq_portal_without_interthread/test.pb.h"
#include "goby/util/debug_logger.h"

#include <memory>

#include <zmq.hpp>

using namespace goby::test::zeromq::protobuf;

// tests ZMQTransporter directly without InterThread

// initially publish one, then wait for queues to be established
int publish_count = 0;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);

using goby::glog;
using namespace goby::util::logger;

extern constexpr goby::middleware::Group sample1{"Sample1"};
extern constexpr goby::middleware::Group sample2{"Sample2"};
extern constexpr goby::middleware::Group widget{"Widget"};

// parent process - thread 1
void publisher(const goby::zeromq::protobuf::InterProcessPortalConfig& cfg)
{
    goby::zeromq::InterProcessPortal<> zmq(cfg);
    zmq.ready();

    double a = 0;
    while (publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        zmq.publish<sample1>(s1);
        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        zmq.publish<sample2>(s2);
        auto w1 = std::make_shared<Widget>();
        w1->set_b(s1->a() - 8);
        zmq.publish<widget>(w1);

        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;

        ++publish_count;
    }

    while (forward) { zmq.poll(std::chrono::milliseconds(10)); }
}

// child process
void handle_sample1(const Sample& sample)
{
    glog.is(DEBUG1) && glog << "InterProcessPortal received publication sample1: "
                            << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void handle_sample2(std::shared_ptr<const Sample> sample)
{
    glog.is(DEBUG1) && glog << "InterProcessPortal received publication sample2: "
                            << sample->ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void handle_widget(const Widget& widget)
{
    glog.is(DEBUG1) && glog << "InterProcessPortal received publication widget: "
                            << widget.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void subscriber(const goby::zeromq::protobuf::InterProcessPortalConfig& cfg)
{
    glog.is(DEBUG1) && glog << "Subscriber InterProcessPortal constructing" << std::endl;
    goby::zeromq::InterProcessPortal<> zmq(cfg);
    glog.is(DEBUG1) && glog << "Subscriber InterProcessPortal constructed" << std::endl;
    zmq.subscribe<sample1, Sample>(&handle_sample1);
    zmq.subscribe<sample2, Sample>(&handle_sample2);
    zmq.subscribe<widget, Widget>(&handle_widget);
    zmq.ready();
    while (ipc_receive_count < 3 * max_publish)
    {
        glog.is(DEBUG1) && glog << ipc_receive_count << "/" << 3 * max_publish << std::endl;
        zmq.poll();
    }
    glog.is(DEBUG1) && glog << "Subscriber complete." << std::endl;
}

int main(int /*argc*/, char* argv[])
{
    goby::zeromq::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test4");
    cfg.set_transport(goby::zeromq::protobuf::InterProcessPortalConfig::TCP);
    cfg.set_ipv4_address("127.0.0.1");
    cfg.set_tcp_port(54325);

    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);

    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);

    std::string os_name =
        std::string("/tmp/goby_test_middleware4_") + (is_child ? "subscriber" : "publisher");
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    std::unique_ptr<std::thread> t2, t3;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;
    if (!is_child)
    {
        manager_context = std::make_unique<zmq::context_t>(1);
        router_context = std::make_unique<zmq::context_t>(10);

        goby::zeromq::protobuf::InterProcessManagerHold hold;
        hold.add_required_client("subscriber");
        hold.add_required_client("publisher");

        goby::zeromq::Router router(*router_context, cfg);
        t2 = std::make_unique<std::thread>([&] { router.run(); });
        goby::zeromq::Manager manager(*manager_context, cfg, router, hold);
        t3 = std::make_unique<std::thread>([&] { manager.run(); });

        auto pub_cfg = cfg;
        pub_cfg.set_client_name("publisher");
        std::thread t1([&] { publisher(pub_cfg); });
        int wstatus;
        wait(&wstatus);
        forward = false;
        t1.join();
        router_context.reset();
        manager_context.reset();
        t2->join();
        t3->join();
        if (wstatus != 0)
            exit(EXIT_FAILURE);
    }
    else
    {
        auto sub_cfg = cfg;
        sub_cfg.set_client_name("subscriber");
        std::thread t1([&] { subscriber(sub_cfg); });
        t1.join();
    }

    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed"
                             << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
