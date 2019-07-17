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

#include <sys/types.h>
#include <sys/wait.h>

#include <atomic>
#include <deque>

#include <boost/units/io.hpp>

#include "goby/middleware/serialize_parse_protobuf.h"
#include "goby/middleware/transport.h"
#include "goby/util/debug_logger.h"
#include "goby/zeromq/transport-interprocess.h"

#include "test.pb.h"

// speed test for interprocess
//#define LARGE_MESSAGE

int publish_count = 0;
int ipc_receive_count = {0};

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);
int test = 1;
goby::middleware::InterThreadTransporter interthread1;
goby::middleware::InterThreadTransporter interthread2;

std::atomic<double> start(0);
std::atomic<double> end(0);

std::mutex cout_mutex;

using goby::glog;
using namespace goby::util::logger;

constexpr goby::middleware::Group sample1_group{"Sample1"};

#ifdef LARGE_MESSAGE
using Type = goby::test::zeromq::protobuf::Large;
const int max_publish = 1000;
#else

using Type = goby::test::zeromq::protobuf::Sample;
const int max_publish = 1000;
#endif

// parent process - thread 1
void publisher(const goby::zeromq::protobuf::InterProcessPortalConfig& cfg)
{
    int a = 0;
    if (test == 0)
    {
        sleep(2);

        {
            std::lock_guard<decltype(cout_mutex)> lock(cout_mutex);
            start = goby::time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);
            std::cout << "Start: " << std::setprecision(15) << start << std::endl;
        }

        while (publish_count < max_publish)
        {
            std::shared_ptr<Type> s = std::make_shared<Type>();
#ifdef LARGE_MESSAGE
            s->set_data(std::string(1000000, 'A'));
#else
            s->set_temperature(a++);
            s->set_salinity(30.1);
            s->set_depth(5.2);
#endif
            interthread1.publish<sample1_group>(s);
            ++publish_count;
        }

        {
            std::lock_guard<decltype(cout_mutex)> lock(cout_mutex);
            std::cout << "Publish end: " << std::setprecision(15)
                      << goby::time::SystemClock::now<goby::time::SITime>() << std::endl;
        }
    }
    else if (test == 1)
    {
        goby::zeromq::InterProcessPortal<> zmq(cfg);
        sleep(1);

        std::cout << "Start: " << std::setprecision(15)
                  << goby::time::SystemClock::now<goby::time::SITime>() << std::endl;

        while (publish_count < max_publish)
        {
            Type s;
#ifdef LARGE_MESSAGE
            s.set_data(std::string(1000000, 'A'));
#else
            s.set_temperature(a++);
            s.set_salinity(30.1);
            s.set_depth(5.2);
#endif
            zmq.publish<sample1_group>(s);

            ++publish_count;
        }

        std::cout << "Publish end: " << std::setprecision(15)
                  << goby::time::SystemClock::now<goby::time::SITime>() << std::endl;

        while (forward) { zmq.poll(std::chrono::milliseconds(100)); }
    }
}

// child process
void handle_sample1(const Type& sample)
{
    if (ipc_receive_count == 0)
    {
        std::lock_guard<decltype(cout_mutex)> lock(cout_mutex);
        std::cout << "Receive start: " << std::setprecision(15)
                  << goby::time::SystemClock::now<goby::time::SITime>() << std::endl;
    }

    //std::cout << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;

    //    if((ipc_receive_count % 100000) == 0)
    //    std::cout << ipc_receive_count << std::endl;

    if (ipc_receive_count == max_publish)
    {
        std::lock_guard<decltype(cout_mutex)> lock(cout_mutex);
        end = goby::time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);
        std::cout << "End: " << std::setprecision(15) << end << std::endl;
        if (test == 0)
            std::cout << "Seconds per message: " << std::setprecision(15)
                      << (end - start) / max_publish << std::endl;
    }
}

void subscriber(const goby::zeromq::protobuf::InterProcessPortalConfig& cfg)
{
    if (test == 0)
    {
        interthread2.subscribe<sample1_group, Type>(&handle_sample1);

        {
            std::lock_guard<decltype(cout_mutex)> lock(cout_mutex);
            std::cout << "Subscribed. " << std::endl;
        }

        while (ipc_receive_count < max_publish) { interthread2.poll(); }
    }
    else if (test == 1)
    {
        goby::zeromq::InterProcessPortal<> zmq(cfg);
        zmq.subscribe<sample1_group, Type>(&handle_sample1);
        std::cout << "Subscribed. " << std::endl;
        while (ipc_receive_count < max_publish) { zmq.poll(); }
    }
}

int main(int argc, char* argv[])
{
    if (argc == 2)
        test = std::stoi(argv[1]);

    std::cout << "Running test type (0 = interthread, 1 = interprocess): " << test << std::endl;

    goby::zeromq::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test6_" + std::to_string(test));
    //    cfg.set_transport(goby::zeromq::protobuf::InterProcessPortalConfig::TCP);
    // cfg.set_ipv4_address("127.0.0.1");
    //cfg.set_tcp_port(10005);
    cfg.set_send_queue_size(max_publish);
    cfg.set_receive_queue_size(max_publish);

    pid_t child_pid = 0;
    bool is_child = false;
    if (test == 1)
    {
        child_pid = fork();
        is_child = (child_pid == 0);
    }

    // goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);

    //std::string os_name = std::string("/tmp/goby_test_middleware4_") + (is_child ? "subscriber" : "publisher");
    //std::ofstream os(os_name.c_str());
    //goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    //goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    //goby::glog.set_lock_action(goby::util::logger_lock::lock);

    if (!is_child)
    {
        std::unique_ptr<std::thread> t10, t11;
        std::unique_ptr<zmq::context_t> manager_context;
        std::unique_ptr<zmq::context_t> router_context;

        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::zeromq::Router router(*router_context, cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::zeromq::Manager manager(*manager_context, cfg, router);
        t11.reset(new std::thread([&] { manager.run(); }));
        //        sleep(1);
        std::thread t1([&] { publisher(cfg); });
        int wstatus = 0;
        if (test == 0)
        {
            std::thread t2([&] { subscriber(cfg); });
            t2.join();
        }
        else
        {
            wait(&wstatus);
        }

        forward = false;
        t1.join();
        manager_context.reset();
        router_context.reset();
        t10->join();
        t11->join();
        if (wstatus != 0)
            exit(EXIT_FAILURE);
    }
    else
    {
        std::thread t1([&] { subscriber(cfg); });
        t1.join();
    }

    //    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
