// Copyright 2017-2020:
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
#include "goby/middleware/transport/interthread.h"
#include "goby/util/debug_logger.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/test/zeromq/middleware_regex/test.pb.h"

#include <zmq.hpp>

using namespace goby::test::zeromq::protobuf;

// tests InterProcessForwarder

goby::middleware::InterThreadTransporter inproc1;
goby::middleware::InterThreadTransporter inproc2;
goby::middleware::InterThreadTransporter inproc3;

int publish_count = 0;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<bool> forward(true);
std::atomic<bool> zmq_ready(false);

using goby::glog;
using namespace goby::util::logger;

constexpr goby::middleware::Group sample1{"Sample1"};
constexpr goby::middleware::Group sample2{"Sample2"};
constexpr goby::middleware::Group widget{"Widget"};
constexpr goby::middleware::Group sample_special_chars{"[Sample]()"};

// thread 1 - parent process
void publisher()
{
    goby::middleware::InterProcessForwarder<goby::middleware::InterThreadTransporter> ipc(inproc1);
    double a = 0;
    while (publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        s1->set_group(sample1);
        ipc.publish<sample1>(s1);

        auto ssc = std::make_shared<Sample>();
        ssc->set_a(a);
        ssc->set_group(sample_special_chars);
        ipc.publish<sample_special_chars>(ssc);

        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        s2->set_group(sample2);
        ipc.publish<sample2>(s2);

        auto w1 = std::make_shared<Widget>();
        w1->set_b(s1->a() - 8);
        ipc.publish<widget>(w1);
        ++publish_count;
    }
}

// thread 1 - child process
void handle_all(const std::vector<unsigned char>& data, int scheme, const std::string& type,
                const goby::middleware::Group& group)
{
    glog.is(DEBUG1) && glog << "InterProcessForwarder received publication of " << data.size()
                            << " bytes from group: " << group << " of type: " << type
                            << " from scheme: " << scheme << std::endl;
    ++ipc_receive_count;
}

void subscriber()
{
    goby::middleware::InterProcessForwarder<goby::middleware::InterThreadTransporter> ipc(inproc1);
    ipc.subscribe_regex(&handle_all, {goby::middleware::MarshallingScheme::ALL_SCHEMES});

    ipc.subscribe_type_regex<sample1, google::protobuf::Message>(
        [&](std::shared_ptr<const google::protobuf::Message> msg, const std::string& type) {
            glog.is(DEBUG1) &&
                glog << "(template) InterProcessForwarder received publication of type: " << type
                     << " with values: " << msg->ShortDebugString() << std::endl;
            assert(type == "goby.test.zeromq.protobuf.Sample");
            auto* s = dynamic_cast<const Sample*>(msg.get());
            assert(s->group() == std::string(sample1));
        },
        ".*Sample");

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while (ipc_receive_count < 4 * max_publish)
    {
        ipc.poll(std::chrono::seconds(1));
        if (std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog << "InterProcessForwarder timed out waiting for data" << std::endl;
    }
}

// thread 3
void zmq_forward(const goby::zeromq::protobuf::InterProcessPortalConfig& cfg)
{
    bool non_template_receive = false;
    bool template_receive = false;
    bool special_chars_receive = false;

    goby::zeromq::InterProcessPortal<goby::middleware::InterThreadTransporter> ipc(inproc3, cfg);
    ipc.subscribe_regex(
        [&](const std::vector<unsigned char>& data, int scheme, const std::string& type,
            const goby::middleware::Group& group) {
            glog.is(DEBUG1) && glog << "InterProcessPortal received publication of " << data.size()
                                    << " bytes from group: " << group << " of type: " << type
                                    << " from scheme: " << scheme << std::endl;
            assert(type == "goby.test.zeromq.protobuf.Sample");
            assert(scheme == goby::middleware::MarshallingScheme::PROTOBUF);
            non_template_receive = true;
        },
        {goby::middleware::MarshallingScheme::PROTOBUF}, ".*Sample", "Sample1|Sample2");

    ipc.subscribe_type_regex<sample1, google::protobuf::Message>(
        [&](std::shared_ptr<const google::protobuf::Message> msg, const std::string& type) {
            glog.is(DEBUG1) &&
                glog << "(template) InterProcessPortal received publication of type: " << type
                     << " with values: " << msg->ShortDebugString() << std::endl;
            assert(type == "goby.test.zeromq.protobuf.Sample");
            auto* s = dynamic_cast<const Sample*>(msg.get());
            assert(s->group() == std::string(sample1));
            template_receive = true;
        },
        ".*Sample");

    ipc.subscribe_type_regex<sample_special_chars, google::protobuf::Message>(
        [&](std::shared_ptr<const google::protobuf::Message> msg, const std::string& type) {
            glog.is(DEBUG1) &&
                glog << "(special chars) InterProcessPortal received publication of type: " << type
                     << " with values: " << msg->ShortDebugString() << std::endl;
            assert(type == "goby.test.zeromq.protobuf.Sample");
            auto* s = dynamic_cast<const Sample*>(msg.get());
            assert(s->group() == std::string(sample_special_chars));
            special_chars_receive = true;
        },
        ".*Sample");

    zmq_ready = true;
    while (forward) { ipc.poll(std::chrono::milliseconds(100)); }

    assert(non_template_receive);
    assert(template_receive);
    assert(special_chars_receive);
}

int main(int argc, char* argv[])
{
    goby::zeromq::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test4");

    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);

    //    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);

    std::string os_name =
        std::string("/tmp/goby_test_middleware_regex_") + (is_child ? "subscriber" : "publisher");
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    std::unique_ptr<std::thread> t4, t5;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;
    if (!is_child)
    {
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::zeromq::Router router(*router_context, cfg);
        t4.reset(new std::thread([&] { router.run(); }));
        goby::zeromq::Manager manager(*manager_context, cfg, router);
        t5.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        std::thread t3([&] { zmq_forward(cfg); });
        while (!zmq_ready) usleep(1e5);

        // wait for subscriber
        sleep(1);
        std::thread t1(publisher);
        t1.join();
        int wstatus;
        wait(&wstatus);
        forward = false;
        t3.join();
        manager_context.reset();
        router_context.reset();
        t4->join();
        t5->join();
        if (wstatus != 0)
            exit(EXIT_FAILURE);
    }
    else
    {
        std::thread t3([&] { zmq_forward(cfg); });
        while (!zmq_ready) usleep(1e5);
        std::thread t1(subscriber);
        t1.join();
        forward = false;
        t3.join();
    }

    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed"
                             << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
