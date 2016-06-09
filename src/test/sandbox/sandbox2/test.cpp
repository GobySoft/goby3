#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "goby/sandbox/marshalling.h"
#include "test.pb.h"

// tests IntraProcessTransporter

goby::IntraProcessTransporter inproc;

std::atomic<int> receive_count(0);
const int max_receive = 10;

// thread 1
void handle_ctd(std::shared_ptr<const CTDSample> ctd)
{
    std::cout <<  "Received: " << ctd->DebugString() << std::endl;
    receive_count++;
}

void publisher()
{
    std::thread::id this_id = std::this_thread::get_id();
    double sal = 38;
    while(receive_count < max_receive)
    {
        auto s = std::make_shared<CTDSample>();
        s->set_salinity(sal++);
        inproc.publish(s, "CTD");
        //        sleep(1);
    }    
}

// thread 2
void subscriber()
{
    std::thread::id this_id = std::this_thread::get_id();
    inproc.subscribe<CTDSample>("CTD", &handle_ctd, this_id);
    while(receive_count < max_receive)
    {
        inproc.poll(this_id);
    }
}


int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    
    std::thread t1(publisher);
    std::thread t2(subscriber);
 
    t1.join();
    t2.join();
    
    std::cout << "all tests passed" << std::endl;
}
