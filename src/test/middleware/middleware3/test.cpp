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

#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/middleware/transport.h"
#include "test.pb.h"

#include <zmq.hpp>

// tests InterProcessForwarder

goby::InterThreadTransporter inproc1;
goby::InterThreadTransporter inproc2;
goby::InterThreadTransporter inproc3;

int publish_count = 0;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<int> ready(0);
std::atomic<bool> forward(true);
std::atomic<bool> zmq_ready(false);

using goby::glog;
using namespace goby::common::logger;

extern constexpr goby::Group sample1{"Sample1"};
extern constexpr goby::Group sample2{"Sample2"};
extern constexpr goby::Group widget{"Widget"};

// thread 1 - parent process
void publisher()
{
    goby::InterProcessForwarder<goby::InterThreadTransporter> ipc(inproc1);
    double a = 0;
    while(publish_count < max_publish)
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
    glog.is(DEBUG1) && glog <<  "InterProcess received publication: " << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void handle_sample2(const Sample& sample)
{
    glog.is(DEBUG1) && glog <<  "InterProcess received publication: " << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}


void handle_widget(std::shared_ptr<const Widget> widget)
{
    glog.is(DEBUG1) && glog <<  "InterProcess received publication: " << widget->ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void subscriber()
{
    goby::InterProcessForwarder<goby::InterThreadTransporter> ipc(inproc1);
    ipc.subscribe<sample1, Sample>(&handle_sample1);
    ipc.subscribe<sample2, Sample>(&handle_sample2);
    ipc.subscribe<widget, Widget>(&handle_widget);

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while(ipc_receive_count < 3*max_publish)
    {
        ipc.poll(std::chrono::seconds(1));
        if(std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog <<  "InterProcessForwarder timed out waiting for data" << std::endl;
    }
}




// thread(s) 2

class ThreadSubscriber
{
public:
    void run()
        {
            inproc2.subscribe<sample1, Sample>([&](const Sample& s) { handle_sample1(s); });
            inproc2.subscribe<sample2, Sample>([&](std::shared_ptr<const Sample> s) { handle_sample2(s); });
            inproc2.subscribe<widget, Widget>([&](std::shared_ptr<const Widget> w) { handle_widget1(w); });
            ++ready;
            while(receive_count1 < max_publish || receive_count2 < max_publish || receive_count3 < max_publish)
            {
                std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
                std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);

                inproc2.poll(std::chrono::seconds(1));
                if(std::chrono::system_clock::now() > timeout)
                    glog.is(DIE) && glog <<  "ThreadSubscriber timed out waiting for data" << std::endl;
            }
            glog.is(DEBUG1) && glog << "ThreadSubscriber  " <<  std::this_thread::get_id() << " is done." << std::endl;
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
            assert(sample->a() == receive_count2+10);
            ++receive_count2;
        }

    void handle_widget1(std::shared_ptr<const Widget> widget)
        {
            std::thread::id this_id = std::this_thread::get_id();
            glog.is(DEBUG1) && glog << this_id << ": Received3: " << widget->DebugString() << std::endl;
            assert(widget->b() == receive_count3-8);
            ++receive_count3;
        }

private:
    int receive_count1 = {0};
    int receive_count2 = {0};
    int receive_count3 = {0};
};


// thread 3
void zmq_forward(const goby::protobuf::InterProcessPortalConfig& cfg)
{
    goby::InterProcessPortal<goby::InterThreadTransporter> zmq(inproc3, cfg);
    zmq.subscribe<sample1, Sample>([&](const Sample& s) { glog.is(DEBUG1) && glog << "Portal Received1: " << s.DebugString() << std::endl; });
    zmq.subscribe<sample2, Sample>([&](std::shared_ptr<const Sample> s) { glog.is(DEBUG1) && glog << "Portal Received2: " << s->DebugString()  << std::endl; });
    zmq.subscribe<widget, Widget>([&](std::shared_ptr<const Widget> w) {  glog.is(DEBUG1) && glog << "Portal Received3: " << w->DebugString()  << std::endl;  });

    zmq_ready = true;
    while(forward)
    {
        zmq.poll(std::chrono::milliseconds(100));
    }
}


int main(int argc, char* argv[])
{
    goby::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test4");
    
    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);

    //    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);

    std::string os_name = std::string("/tmp/goby_test_middleware3_") + (is_child ? "subscriber" : "publisher");
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::common::logger::DEBUG3, &os);
    goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    goby::glog.set_lock_action(goby::common::logger_lock::lock);
                        
    //    std::thread t3(subscriber);
    const int max_subs = 3;
    std::vector<ThreadSubscriber> thread_subscribers(max_subs, ThreadSubscriber());
    std::vector<std::thread> threads;
    for(int i = 0; i < max_subs; ++i)
    {
        threads.push_back(std::thread(std::bind(&ThreadSubscriber::run, &thread_subscribers.at(i))));
    }

    while(ready < max_subs)
        usleep(1e5);

    std::unique_ptr<std::thread> t4, t5;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;
    if(!is_child)
    {
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::ZMQRouter router(*router_context, cfg);
        t4.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, cfg, router);
        t5.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        std::thread t3([&] { zmq_forward(cfg); });
        while(!zmq_ready)
            usleep(1e5);
        std::thread t1(publisher);
        t1.join();
        for(int i = 0; i < max_subs; ++i)
            threads.at(i).join();
        int wstatus;
        wait(&wstatus);
        forward = false;
        t3.join();
        manager_context.reset();
        router_context.reset();
        t4->join();
        t5->join();
        if(wstatus != 0) exit(EXIT_FAILURE);
    }
    else
    {
        std::thread t3([&] { zmq_forward(cfg); });
        while(!zmq_ready)
            usleep(1e5);
        std::thread t1(subscriber);
        t1.join();
        for(int i = 0; i < max_subs; ++i)
            threads.at(i).join();
        forward = false;
        t3.join();

    }

    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
