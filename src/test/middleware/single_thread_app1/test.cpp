#include "goby/middleware/single-thread-application.h"
#include <boost/units/io.hpp>

#include "test.pb.h"
using goby::glog;
using namespace goby::common::logger;

extern constexpr goby::Group widget1{"Widget1"};

using Base = goby::SingleThreadApplication<TestConfig>;
class TestApp : public Base
{
public:
    TestApp() : Base(10)
        {
            portal().subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
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
                assert(rx_count_ == tx_count_);
                std::cout << goby::common::goby_time() << std::endl;
                Widget w;
                w.set_b(++tx_count_);
                std::cout << "Tx: " << w.DebugString() << std::flush;
                portal().publish<widget1>(w);
            }
            
        }

    void post(const Widget& widget)
        {
            std::cout << "Rx: " << widget.DebugString() << std::flush;
            assert(widget.b() == tx_count_);
            ++rx_count_;
        }
    
    
private:
    int tx_count_ { 0 };
    int rx_count_ { 0 };
    
};
    


int main(int argc, char* argv[])
{ return goby::run<TestApp>(argc, argv); }

