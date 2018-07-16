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

#include "goby/middleware/multi-thread-application.h"
#include "goby/common/time.h"

#include <boost/units/io.hpp>
#include <sys/types.h>
#include <sys/wait.h>

#include "test.pb.h"
using goby::glog;
using namespace goby::common::logger;

extern constexpr goby::Group widget1{3};
extern constexpr goby::Group widget2{"widget2"};
extern constexpr goby::Group ready{"ready"};

constexpr int num_messages{10};


using AppBase = goby::MultiThreadApplication<TestConfig>;

class TestThreadRx : public goby::SimpleThread<TestConfig>
{
public:
    TestThreadRx(const TestConfig& cfg)
        : SimpleThread(cfg, .1)
        {
            glog.is(VERBOSE) && glog << "Rx Thread: pid: " << getpid() << ", thread: " << std::this_thread::get_id() << std::endl;
            
            glog.is(VERBOSE) && glog << std::this_thread::get_id() << std::endl;
            
            interprocess().subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
            interprocess().subscribe<widget2, Widget>([this](const Widget& w) { post(w); });
        }

    ~TestThreadRx()
        {            
        }
        
    
    void post(const Widget& widget)
        {
            glog.is(VERBOSE) && glog << "Thread Rx: " << widget.DebugString() << std::flush;
            assert(widget.b() == rx_count_);
            ++rx_count_;

            interthread().publish<widget2>(widget);


            throw(std::runtime_error("test"));
        }
    
    void loop() override
        {            
        }

private:
    int rx_count_{0};
};

class TestAppRx : public AppBase
{
public:
    TestAppRx() : AppBase(10)
        {
            glog.is(VERBOSE) && glog << "Rx App: pid: " << getpid() << ", thread: " << std::this_thread::get_id() << std::endl;
            interprocess().subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
            interprocess().subscribe<widget2, Widget>([this](const Widget& w) { post2(w); });
            launch_thread<TestThreadRx>();
        }
    
    void loop() override
        {
            if(rx_count_ == 0)
            {
                Ready r;
                r.set_b(true);
                interprocess().publish<ready>(r);
            }
        }

    void post(const Widget& widget)
        {
            glog.is(VERBOSE) && glog << "App Rx: " << widget.DebugString() << std::flush;
            assert(widget.b() == rx_count_);
            ++rx_count_;
            if(rx_count_ == num_messages)
                quit();
        }    

    void post2(const Widget& widget)
        {
            glog.is(VERBOSE) && glog << "App Rx2: " << widget.DebugString() << std::flush;
        }
    
    
private:
    int rx_count_{0};
};


class TestAppTx : public AppBase
{
public:
    TestAppTx() : AppBase(100)
        {
            glog.is(VERBOSE) && glog << "Tx App: pid: " << getpid() << ", thread: " << std::this_thread::get_id() << std::endl;
            interprocess().subscribe<ready, Ready>([this](const Ready& r) { rx_ready_ = r.b(); });

        }

    void loop() override
        {
            static int i = 0;
            ++i;
            if(rx_ready_)
            {
                glog.is(VERBOSE) && glog  << goby::common::goby_time() << std::endl;
                Widget w;
                w.set_b(tx_count_++);
                {
                    glog.is(VERBOSE) && glog << "Tx: " << w.DebugString() << std::flush;
                }
                
                interprocess().publish<widget1>(w);

                if(tx_count_ == (num_messages+5))
                    quit();
            }
            
        }

    
private:
    int tx_count_ {0};
    bool rx_ready_ {false};
    
};



int main(int argc, char* argv[])
{
    int child_pid = fork();
    
    std::unique_ptr<std::thread> t2, t3;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;

    if(child_pid != 0)
    {
        goby::protobuf::InterProcessPortalConfig cfg;
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));
        goby::ZMQRouter router(*router_context, cfg);
        t2.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, cfg, router);
        t3.reset(new std::thread([&] { manager.run(); }));
        int wstatus;
        wait(&wstatus);        
        router_context.reset();
        manager_context.reset();
        t2->join();
        t3->join();
        if(wstatus != 0) exit(EXIT_FAILURE);
    }
    else
    {
        int child2_pid = fork();
        if(child2_pid != 0)
        {
            int wstatus;
            int rc = goby::run<TestAppRx>(argc, argv);
            wait(&wstatus);        
            if(wstatus != 0) exit(EXIT_FAILURE);
            return rc;
        }
        else
        {
            usleep(100000);
            return goby::run<TestAppTx>(argc, argv);
        }
        
    }
    std::cout << "All tests passed." << std::endl;
    
}
    
