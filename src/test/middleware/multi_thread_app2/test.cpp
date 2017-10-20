#include "goby/middleware/multi-thread-application.h"
#include <boost/units/io.hpp>
#include <sys/types.h>
#include <sys/wait.h>

#include "test.pb.h"
using goby::glog;
using namespace goby::common::logger;

extern constexpr goby::Group widget1{3};

using AppBase = goby::MultiThreadApplication<TestConfig>;

std::atomic<int> complete{0};
std::atomic<int> ready{0};

class TestThreadRx : public goby::SimpleThread<TestConfig>
{
public:
    TestThreadRx(const TestConfig& cfg, int index)
        : SimpleThread(cfg, 0, index)
        {
            glog.is(VERBOSE) && glog << "Rx Thread: pid: " << getpid() << ", thread: " << std::this_thread::get_id() << std::endl;
            
            glog.is(VERBOSE) && glog << "Subscribing: rx thread: " << std::this_thread::get_id() << std::endl;
            interthread().subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
            glog.is(VERBOSE) && glog << "...subscribed: rx thread: " << std::this_thread::get_id() << std::endl;
            ++ready;
        }

    void post(const Widget& widget)
        {
            // assert(widget.b() == rx_count_);
            ++rx_count_;
            

            
            if(rx_count_ == cfg().num_tx_threads()*cfg().num_messages())
            {
                glog.is(VERBOSE) && glog << "Rx thread: " << std::this_thread::get_id() << ": complete" << std::endl;
                ++complete;
                thread_quit();
            }
        }

private:
    int rx_count_{0};
};

class TestThreadTx : public goby::SimpleThread<TestConfig>
{
public:
    TestThreadTx(const TestConfig& cfg, int index)
        : SimpleThread(cfg, 100000, index)
        {
            glog.is(VERBOSE) && glog << "Tx Thread: pid: " << getpid() << ", thread: " << std::this_thread::get_id() << std::endl;            
        }


    void loop() override
        {
            if(ready < cfg().num_rx_threads())
                return;
            
            std::shared_ptr<Widget> w(new Widget());
            w->set_b(tx_count_);
            interthread().publish<widget1, Widget>(w);
            ++tx_count_;
        }
private:
    int tx_count_{0};
};


class TestApp : public AppBase
{
public:
    TestApp() : AppBase(10)
        {
            for(int i = 0; i < cfg().num_rx_threads(); ++i)
                launch_thread<TestThreadRx>(i);
            for(int i = 0; i < cfg().num_tx_threads(); ++i)
                launch_thread<TestThreadTx>(i);

            start_ = goby::common::goby_time<goby::uint64>();
            glog.is(VERBOSE) && glog << "Start: " << start_ << std::endl;
        }
    
    void loop() override
        {
            if(complete == cfg().num_rx_threads())
            {
                goby::uint64 end = goby::common::goby_time<goby::uint64>();
                glog.is(VERBOSE) && glog << "End: " << end << std::endl;
                glog.is(VERBOSE) && glog << "Microseconds per message: " << (end-start_)/cfg().num_messages() << std::endl;

                for(int i = 0; i < cfg().num_rx_threads(); ++i)
                    join_thread<TestThreadRx>(i);
                for(int i = 0; i < cfg().num_tx_threads(); ++i)
                    join_thread<TestThreadTx>(i);

                std::cout << cfg().num_rx_threads() << " " << (end-start_)/cfg().num_messages() << std::endl;
                quit();
            }
        }
    
private:
    goby::uint64 start_{0};
    
};


int main(int argc, char* argv[])
{
    goby::run<TestApp>(argc, argv);
}
    
