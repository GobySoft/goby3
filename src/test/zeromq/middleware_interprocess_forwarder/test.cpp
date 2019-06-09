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

#include "goby/middleware/transport.h"
#include "goby/zeromq/transport-interprocess.h"

#include "goby/util/debug_logger.h"
#include "test.pb.h"

#include <zmq.hpp>

// tests InterProcessForwarder

// avoid static initialization order problem
goby::InterProcessForwarder<goby::InterThreadTransporter>& ipc_child()
{
    static std::unique_ptr<goby::InterThreadTransporter> inner(new goby::InterThreadTransporter);
    static std::unique_ptr<goby::InterProcessForwarder<goby::InterThreadTransporter> > p(
        new goby::InterProcessForwarder<goby::InterThreadTransporter>(*inner));
    return *p;
}

int publish_count = 0;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<int> ready(0);
std::atomic<bool> forward(true);
std::atomic<bool> zmq_ready(false);

using goby::glog;
using namespace goby::util::logger;

extern constexpr goby::Group sample1{"Sample1"};
extern constexpr goby::Group sample2{"Sample2"};
extern constexpr goby::Group widget{"Widget"};

// thread 1 - parent process
void publisher()
{
    goby::InterThreadTransporter inproc1;
    goby::InterProcessForwarder<goby::InterThreadTransporter> ipc(inproc1);
    double a = 0;
    while (
        publish_count <
        max_publish *
            2) // double the amount of publication to handle the (inherent) slop in unsubscribe/subscribe
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        ipc.publish<sample1>(s1);
        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        ipc.publish<sample2>(s2);
        auto w1 = std::make_shared<Widget>();
        w1->set_b(s1->a() - 8);
        ipc.publish<widget>(w1);
        ++publish_count;
    }
}

// thread 1 - child process

void handle_sample1(const Sample& sample)
{
    static int receive_count1 = 0;
    glog.is(DEBUG1) &&
        glog << "InterProcess sample1 received publication: " << sample.ShortDebugString()
             << ", receive_count1: " << receive_count1 << std::endl;

    if (receive_count1 < max_publish / 2)
        assert(sample.a() == receive_count1);
    // commented out, since depending on timing, there may be additional samples in the buffer
    //    else // skip 10
    //assert(sample.a() == receive_count1 + 10);

    ++ipc_receive_count;
    ++receive_count1;

    if (receive_count1 == max_publish / 2)
    {
        glog.is(DEBUG1) && glog << "Sample 1 unsubscribe" << std::endl;
        ipc_child().unsubscribe<sample1, Sample>();
    }
}

void handle_sample2(const Sample& sample)
{
    static int receive_count2 = 0;
    glog.is(DEBUG1) && glog << "InterProcess sample2 received publication: "
                            << sample.ShortDebugString() << std::endl;
    assert(sample.a() == receive_count2 + 10);
    ++ipc_receive_count;
    ++receive_count2;

    if (receive_count2 == max_publish / 2 + 10)
    {
        glog.is(DEBUG1) && glog << "Sample 1 resubscribe" << std::endl;
        ipc_child().subscribe<sample1, Sample>(&handle_sample1);
    }
}

void handle_widget(std::shared_ptr<const Widget> widget)
{
    static int receive_count3 = 0;
    glog.is(DEBUG1) && glog << "InterProcess widget received publication: "
                            << widget->ShortDebugString() << std::endl;
    assert(widget->b() == receive_count3 - 8);
    ++ipc_receive_count;
    ++receive_count3;
}

void subscriber()
{
    ipc_child().subscribe_dynamic<Sample>(
        [](std::shared_ptr<const Sample> s) { handle_sample1(*s); }, sample1);
    ipc_child().subscribe<sample2, Sample>(&handle_sample2);
    ipc_child().subscribe<widget, Widget>(&handle_widget);

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    // -10 since we unsubscribe for 10 counts for sample1
    while (ipc_receive_count < 3 * max_publish - 10)
    {
        ipc_child().poll(std::chrono::seconds(1));
        if (std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog << "InterProcessForwarder timed out waiting for data" << std::endl;
    }
}

// thread(s) 2

class ThreadSubscriber
{
  public:
    void run()
    {
        inproc2_.subscribe<sample1, Sample>([&](const Sample& s) { handle_sample1(s); });
        inproc2_.subscribe<sample2, Sample>(
            [&](std::shared_ptr<const Sample> s) { handle_sample2(s); });
        inproc2_.subscribe<widget, Widget>(
            [&](std::shared_ptr<const Widget> w) { handle_widget1(w); });
        ++ready;
        while (receive_count1 < max_publish || receive_count2 < (max_publish / 2) ||
               receive_count3 < max_publish)
        {
            std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
            std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);

            inproc2_.poll(std::chrono::seconds(1));
            if (std::chrono::system_clock::now() > timeout)
                glog.is(DIE) && glog << "ThreadSubscriber " << std::this_thread::get_id()
                                     << " timed out waiting for data" << std::endl;
        }
        glog.is(DEBUG1) && glog << "ThreadSubscriber  " << std::this_thread::get_id() << " is done."
                                << std::endl;
    }

  private:
    void handle_sample1(Sample sample)
    {
        std::thread::id this_id = std::this_thread::get_id();
        glog.is(DEBUG1) && glog << this_id << ": Received1: " << sample.DebugString() << std::endl;
        assert(sample.a() == receive_count1);
        ++receive_count1;
    }
    void handle_sample2(std::shared_ptr<const Sample> sample)
    {
        std::thread::id this_id = std::this_thread::get_id();
        glog.is(DEBUG1) && glog << this_id << ": Received2: " << sample->DebugString() << std::endl;

        assert(sample->a() == receive_count2 + 10);

        ++receive_count2;

        if (receive_count2 == max_publish / 2)
        {
            glog.is(DEBUG1) && glog << this_id << ": Sample 2 unsubscribe" << std::endl;
            inproc2_.unsubscribe<sample2, Sample>();
        }
    }

    void handle_widget1(std::shared_ptr<const Widget> widget)
    {
        std::thread::id this_id = std::this_thread::get_id();
        glog.is(DEBUG1) && glog << this_id << ": Received3: " << widget->DebugString() << std::endl;
        assert(widget->b() == receive_count3 - 8);
        ++receive_count3;
    }

  private:
    int receive_count1 = {0};
    int receive_count2 = {0};
    int receive_count3 = {0};
    goby::InterThreadTransporter inproc2_;
};

// thread 3
void zmq_forward(const goby::zeromq::protobuf::InterProcessPortalConfig& cfg)
{
    goby::InterThreadTransporter inproc3;
    goby::zeromq::InterProcessPortal<goby::InterThreadTransporter> zmq(inproc3, cfg);
    zmq.subscribe<sample1, Sample>([&](const Sample& s) {
        glog.is(DEBUG1) && glog << "Portal Received1: " << s.DebugString() << std::endl;
        if (s.a() == 3 * max_publish / 4)
            zmq.unsubscribe<sample1, Sample>();

        assert(s.a() <= 3 * max_publish / 4);
    });
    zmq.subscribe<sample2, Sample>([&](std::shared_ptr<const Sample> s) {
        glog.is(DEBUG1) && glog << "Portal Received2: " << s->DebugString() << std::endl;
    });
    zmq.subscribe<widget, Widget>([&](std::shared_ptr<const Widget> w) {
        glog.is(DEBUG1) && glog << "Portal Received3: " << w->DebugString() << std::endl;
    });

    zmq_ready = true;
    while (forward) { zmq.poll(std::chrono::milliseconds(100)); }
}

int main(int argc, char* argv[])
{
    goby::zeromq::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test3");

    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);
    bool is_subscriber = is_child;

    //    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);

    std::string os_name =
        std::string("/tmp/goby_test_middleware3_") + (is_subscriber ? "subscriber" : "publisher");
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    goby::glog.set_name(std::string(argv[0]) + (is_subscriber ? "_subscriber" : "_publisher"));
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    //    std::thread t3(subscriber);
    const int max_subs = 3;
    std::vector<ThreadSubscriber> thread_subscribers(max_subs, ThreadSubscriber());
    std::vector<std::thread> threads;
    for (int i = 0; i < max_subs; ++i)
    {
        threads.push_back(
            std::thread(std::bind(&ThreadSubscriber::run, &thread_subscribers.at(i))));
    }

    while (ready < max_subs) usleep(1e5);

    std::unique_ptr<std::thread> t4, t5;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;
    if (is_subscriber)
    {
        std::thread t3([&] { zmq_forward(cfg); });
        while (!zmq_ready) usleep(1e5);
        std::thread t1(subscriber);
        t1.join();
        for (int i = 0; i < max_subs; ++i) threads.at(i).join();
        forward = false;
        t3.join();
    }
    else
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
        std::thread t1(publisher);
        t1.join();
        for (int i = 0; i < max_subs; ++i) threads.at(i).join();
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

    glog.is(VERBOSE) && glog << (is_subscriber ? "subscriber" : "publisher") << ": all tests passed"
                             << std::endl;
    std::cout << (is_subscriber ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
