#include "goby/middleware/multi-thread-application.h"
#include <boost/units/io.hpp>
#include <sys/types.h>
#include <sys/wait.h>

#include "test.pb.h"
using goby::glog;
using namespace goby::common::logger;

extern constexpr goby::Group widget1{"Widget1"};

goby::InterThreadTransporter inproc;

using AppBase = goby::MultiThreadApplication<TestConfig>;
using ThreadBase = AppBase::ThreadBase;


class TestThread : public ThreadBase
{
public:
    TestThread() : ThreadBase(&forwarder_, 10)
        {
            forwarder_.subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
        }

    void post(const Widget& widget)
        {
            glog.is(VERBOSE) && glog << "Thread Rx: " << widget.DebugString() << std::flush;

        }
    
    void loop() override
        {
        }

private:
    goby::InterProcessForwarder<goby::InterThreadTransporter> forwarder_ { inproc };
};

class TestApp : public AppBase
{
public:
    TestApp() : AppBase(10)
        {
            //portal().subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
            launch_thread(test_thread_);
        }

    void loop() override
        {
            static int i = 0;
            ++i;
            if(i < 2)
            {
                return;
            }
            else if(i > (std::max(5, 2+(int)loop_frequency_hertz())))
            {
                quit();
            }
            else
            {
                //                assert(rx_count_ == tx_count_);
                glog.is(VERBOSE) && glog  << goby::common::goby_time() << std::endl;
                Widget w;
                w.set_b(++tx_count_);
                {
                    glog.is(VERBOSE) && glog << "Tx: " << w.DebugString() << std::flush;
                }
                
                portal().publish<widget1>(w);
            }
            
        }

    void post(const Widget& widget)
        {
            glog.is(VERBOSE) && glog << "Rx: " << widget.DebugString() << std::flush;
            assert(widget.b() == tx_count_);
            ++rx_count_;
        }
    
    
private:
    int tx_count_ { 0 };
    int rx_count_ { 0 };
    TestThread test_thread_;
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
        sleep(1);

        
        return goby::run<TestApp>(argc, argv);
    }
}
    
