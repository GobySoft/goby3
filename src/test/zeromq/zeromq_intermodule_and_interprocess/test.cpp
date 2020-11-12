// Copyright 2020:
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
#include "goby/zeromq/transport/intermodule.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/util/debug_logger.h"
#include "test.pb.h"

#include <zmq.hpp>

using namespace goby::test::zeromq::protobuf;

// tests InterProcess and InterModule directly (without InterThread)

// initially publish one, then wait for queues to be established
int publish_count = -1;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<bool> running(true);

std::atomic<int> zmq_reqs(0);
std::atomic<int> subscribers_complete(0);
const int n_subscribers = 2;

using goby::glog;
using namespace goby::util::logger;

extern constexpr goby::middleware::Group sample1{"Sample1"};
extern constexpr goby::middleware::Group sample2{"Sample2"};
extern constexpr goby::middleware::Group widget{"Widget"};
extern constexpr goby::middleware::Group complete{"Complete"};

void portal_publisher(const goby::zeromq::protobuf::InterProcessPortalConfig& p_cfg,
                      const goby::zeromq::protobuf::InterProcessPortalConfig& m_cfg)
{
    goby::zeromq::InterProcessPortal<> interprocess(p_cfg);
    goby::zeromq::InterModulePortal<goby::zeromq::InterProcessPortal<>> intermodule(interprocess,
                                                                                    m_cfg);

    intermodule.subscribe<complete, Complete>([](const Complete& complete) {
        glog.is_debug1() && glog << "Subscriber complete: " << complete.ShortDebugString()
                                 << std::endl;
        ++subscribers_complete;
        if (subscribers_complete == n_subscribers)
            running = false;
    });

    double a = 0;
    while (publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        intermodule.publish<sample1>(s1);
        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        intermodule.publish<sample2>(s2);
        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        intermodule.poll(std::chrono::seconds(0));

        if (publish_count < 0)
            usleep(1e6);

        ++publish_count;
    }

    while (running) intermodule.poll(std::chrono::seconds(1));
}

void forwarder_publisher(const goby::zeromq::protobuf::InterProcessPortalConfig& p_cfg)
{
    goby::zeromq::InterProcessPortal<> interprocess(p_cfg);
    goby::middleware::InterModuleForwarder<goby::zeromq::InterProcessPortal<>> intermodule(
        interprocess);

    intermodule.subscribe<complete, Complete>([](const Complete& complete) {
        glog.is_debug1() && glog << "Subscriber complete: " << complete.ShortDebugString()
                                 << std::endl;
        ++subscribers_complete;
        if (subscribers_complete == n_subscribers)
            running = false;
    });

    double a = 0;
    while (publish_count < max_publish)
    {
        auto w1 = std::make_shared<Widget>();
        w1->set_b(a++ - 8);
        intermodule.publish<widget>(w1);

        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;

        intermodule.poll(std::chrono::seconds(0));
        if (publish_count < 0)
            usleep(1e6);

        ++publish_count;
    }

    while (running) intermodule.poll(std::chrono::seconds(1));
}

void portal_subscriber(const goby::zeromq::protobuf::InterProcessPortalConfig& p_cfg,
                       const goby::zeromq::protobuf::InterProcessPortalConfig& m_cfg)
{
    goby::zeromq::InterProcessPortal<> interprocess(p_cfg);
    goby::zeromq::InterModulePortal<goby::zeromq::InterProcessPortal<>> intermodule(interprocess,
                                                                                    m_cfg);

    intermodule.subscribe<complete, Complete>([](const Complete& complete) {
        glog.is_debug1() && glog << "Forwarder subscriber complete: " << complete.ShortDebugString()
                                 << std::endl;

        if (complete.subscriber_id() == 1)
            running = false;
    });

    intermodule.subscribe<sample1, Sample>([](const Sample& sample) {
        glog.is(DEBUG1) && glog << "InterModulePortal received publication sample1: "
                                << sample.ShortDebugString() << std::endl;
        ++ipc_receive_count;
    });

    intermodule.subscribe<sample2, Sample>([](const Sample& sample) {
        glog.is(DEBUG1) && glog << "InterModulePortal received publication sample2: "
                                << sample.ShortDebugString() << std::endl;
        ++ipc_receive_count;
    });

    intermodule.subscribe<widget, Widget>([](const Widget& widget) {
        glog.is(DEBUG1) && glog << "InterModulePortal received publication widget: "
                                << widget.ShortDebugString() << std::endl;
        ++ipc_receive_count;
    });

    while (ipc_receive_count < 3 * max_publish)
    {
        glog.is(DEBUG1) && glog << ipc_receive_count << "/" << 3 * max_publish << std::endl;
        intermodule.poll();
    }

    Complete c;
    c.set_subscriber_id(0);
    intermodule.publish<complete>(c);

    while (running) intermodule.poll(std::chrono::seconds(1));

    sleep(1);
    glog.is(DEBUG1) && glog << "Portal Subscriber complete." << std::endl;
}

void forwarder_subscriber(const goby::zeromq::protobuf::InterProcessPortalConfig& p_cfg)
{
    goby::zeromq::InterProcessPortal<> interprocess(p_cfg);
    goby::middleware::InterModuleForwarder<goby::zeromq::InterProcessPortal<>> intermodule(
        interprocess);

    intermodule.subscribe<sample1, Sample>([](const Sample& sample) {
        glog.is(DEBUG1) && glog << "InterModuleForwarder received publication sample1: "
                                << sample.ShortDebugString() << std::endl;
        ++ipc_receive_count;
    });

    intermodule.subscribe<sample2, Sample>([](const Sample& sample) {
        glog.is(DEBUG1) && glog << "InterModuleForwarder received publication sample2: "
                                << sample.ShortDebugString() << std::endl;
        ++ipc_receive_count;
    });

    intermodule.subscribe<widget, Widget>([](const Widget& widget) {
        glog.is(DEBUG1) && glog << "InterModuleForwarder received publication widget: "
                                << widget.ShortDebugString() << std::endl;
        ++ipc_receive_count;
    });

    while (ipc_receive_count < 3 * max_publish)
    {
        glog.is(DEBUG1) && glog << ipc_receive_count << "/" << 3 * max_publish << std::endl;
        intermodule.poll();
    }

    Complete c;
    c.set_subscriber_id(1);
    intermodule.publish<complete>(c);

    glog.is(DEBUG1) && glog << "Subscriber complete." << std::endl;
}

int main(int argc, char* argv[])
{
    goby::zeromq::protobuf::InterProcessPortalConfig interprocess_cfg1;
    interprocess_cfg1.set_platform("test_interprocess1");
    interprocess_cfg1.set_transport(goby::zeromq::protobuf::InterProcessPortalConfig::TCP);
    interprocess_cfg1.set_ipv4_address("127.0.0.1");
    interprocess_cfg1.set_tcp_port(54326);

    goby::zeromq::protobuf::InterProcessPortalConfig interprocess_cfg2;
    interprocess_cfg2.set_platform("test_interprocess2");
    interprocess_cfg2.set_transport(goby::zeromq::protobuf::InterProcessPortalConfig::TCP);
    interprocess_cfg2.set_ipv4_address("127.0.0.1");
    interprocess_cfg2.set_tcp_port(54327);

    goby::zeromq::protobuf::InterProcessPortalConfig intermodule_cfg;
    intermodule_cfg.set_platform("test_intermodule");
    intermodule_cfg.set_transport(goby::zeromq::protobuf::InterProcessPortalConfig::TCP);
    intermodule_cfg.set_ipv4_address("127.0.0.1");
    intermodule_cfg.set_tcp_port(54328);

    enum Roles
    {
        MANAGER_ROUTER,
        PORTAL_PUBLISHER,
        FORWARDER_PUBLISHER,
        PORTAL_SUBSCRIBER,
        FORWARDER_SUBSCRIBER
    };
    const int n_children = 4;

    Roles role = MANAGER_ROUTER;
    std::string role_str = "manager_router";
    if (fork() == 0)
    {
        role = PORTAL_PUBLISHER;
        role_str = "portal_publisher";
    }
    else if (fork() == 0)
    {
        role = FORWARDER_PUBLISHER;
        role_str = "forwarder_publisher";
    }
    else if (fork() == 0)
    {
        role = PORTAL_SUBSCRIBER;
        role_str = "portal_subscriber";
    }
    else if (fork() == 0)
    {
        role = FORWARDER_SUBSCRIBER;
        role_str = "forwarder_subscriber";
    }

    //    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);

    std::string os_name = std::string("/tmp/goby_test_intermodule_and_interprocess_") + role_str;
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    goby::glog.set_name(std::string(argv[0]) + std::string("_") + role_str);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);
    switch (role)
    {
        case MANAGER_ROUTER:
        {
            std::unique_ptr<std::thread> mt1, mt2, mt3, rt1, rt2, rt3;
            std::unique_ptr<zmq::context_t> manager1_context, manager2_context, manager3_context;
            std::unique_ptr<zmq::context_t> router1_context, router2_context, router3_context;

            manager1_context.reset(new zmq::context_t(1));
            router1_context.reset(new zmq::context_t(10));
            manager2_context.reset(new zmq::context_t(1));
            router2_context.reset(new zmq::context_t(10));
            manager3_context.reset(new zmq::context_t(1));
            router3_context.reset(new zmq::context_t(10));

            goby::zeromq::Router router1(*router1_context, interprocess_cfg1);
            mt1.reset(new std::thread([&] { router1.run(); }));
            goby::zeromq::Manager manager1(*manager1_context, interprocess_cfg1, router1);
            rt1.reset(new std::thread([&] { manager1.run(); }));

            goby::zeromq::Router router2(*router2_context, interprocess_cfg2);
            mt2.reset(new std::thread([&] { router2.run(); }));
            goby::zeromq::Manager manager2(*manager2_context, interprocess_cfg2, router2);
            rt2.reset(new std::thread([&] { manager2.run(); }));

            goby::zeromq::Router router3(*router3_context, intermodule_cfg);
            mt3.reset(new std::thread([&] { router3.run(); }));
            goby::zeromq::Manager manager3(*manager3_context, intermodule_cfg, router3);
            rt3.reset(new std::thread([&] { manager3.run(); }));

            int wstatus;
            for (int i = 0; i < n_children; ++i)
            {
                wait(&wstatus);
                std::cout << "child ended with status: " << wstatus << std::endl;
                if (wstatus != 0)
                    exit(EXIT_FAILURE);
            }

            router1_context.reset();
            manager1_context.reset();
            router2_context.reset();
            manager2_context.reset();
            router3_context.reset();
            manager3_context.reset();
            mt1->join();
            mt2->join();
            mt3->join();
            rt1->join();
            rt2->join();
            rt3->join();

            break;
        }

        case PORTAL_PUBLISHER:
        {
            usleep(1e6);
            std::thread t1([&] { portal_publisher(interprocess_cfg1, intermodule_cfg); });
            t1.join();
            break;
        }

        case FORWARDER_PUBLISHER:
        {
            usleep(1.5e6);
            std::thread t1([&] { forwarder_publisher(interprocess_cfg1); });
            t1.join();
            break;
        }

        case PORTAL_SUBSCRIBER:
        {
            usleep(1e6);
            std::thread t1([&] { portal_subscriber(interprocess_cfg2, intermodule_cfg); });
            t1.join();
            break;
        }

        case FORWARDER_SUBSCRIBER:
        {
            usleep(1.5e6);
            std::thread t1([&] { forwarder_subscriber(interprocess_cfg2); });
            t1.join();
            break;
        }
    }

    glog.is(VERBOSE) && glog << role_str << ": all tests passed" << std::endl;
    std::cout << role_str << ": all tests passed" << std::endl;
}
