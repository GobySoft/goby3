#include <sys/types.h>
#include <sys/wait.h>

#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/middleware/transport.h"
#include "test.pb.h"

#include <zmq.hpp>

// speed test for interprocess
//#define LARGE_MESSAGE

int publish_count = 0;
int ipc_receive_count = {0};

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);
int test = 1;
goby::InterThreadTransporter interthread;

using goby::glog;
using namespace goby::common::logger;

struct TestGroups
{
    static constexpr goby::Group sample1_group{"Sample1"};
};

constexpr goby::Group TestGroups::sample1_group;
    
#ifdef LARGE_MESSAGE
using Type = Large;
const int max_publish = 1000;
#else
using Type = Sample;
const int max_publish = 100000;
#endif
    
// parent process - thread 1
void publisher(const goby::protobuf::InterProcessPortalConfig& cfg)
{
    int a = 0;    
    if(test == 0)
    {
        sleep(2);
        std::cout << "Start: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;
        
        while(publish_count < max_publish)
        {
            std::shared_ptr<Type> s = std::make_shared<Type>();
#ifdef LARGE_MESSAGE
            s->set_data(std::string(1000000, 'A'));
#else
            s->set_temperature(a++);
            s->set_salinity(30.1);
            s->set_depth(5.2);
#endif
            interthread.publish<TestGroups::sample1_group>(s);
            ++publish_count;
        }

        std::cout << "Publish end: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;
    }
    else if(test == 1)
    {
        goby::InterProcessPortal<> zmq(cfg);
        sleep(1);
        std::cout << "Start: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;
        
        while(publish_count < max_publish)
        {
            Type s;
#ifdef LARGE_MESSAGE
            s.set_data(std::string(1000000, 'A'));
#else
            s.set_temperature(a++);
            s.set_salinity(30.1);
            s.set_depth(5.2);            
#endif
            zmq.publish<TestGroups::sample1_group>(s);
            
            ++publish_count;
        }
        
        std::cout << "Publish end: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;

        while(forward)
        {
            zmq.poll(std::chrono::milliseconds(100));
        }
    }
    

}

// child process
void handle_sample1(const Type& sample)
{
    if(ipc_receive_count == 0)
        std::cout << "Receive start: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;

    //std::cout << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
    
    //    if((ipc_receive_count % 100000) == 0)
    //    std::cout << ipc_receive_count << std::endl;
    
    if(ipc_receive_count == max_publish)
        std::cout << "End: " << std::setprecision(15) << goby::common::goby_time<double>() << std::endl;

}

void subscriber(const goby::protobuf::InterProcessPortalConfig& cfg)
{
    if(test == 0)
    {
        interthread.subscribe<TestGroups::sample1_group, Type>(&handle_sample1);
        std::cout << "Subscribed. " << std::endl;

        while(ipc_receive_count < max_publish)
        {
            interthread.poll();
        }
    }
    else if(test == 1)
    {
        goby::InterProcessPortal<> zmq(cfg);
        zmq.subscribe<TestGroups::sample1_group, Type>(&handle_sample1);
        std::cout << "Subscribed. " << std::endl;
        while(ipc_receive_count < max_publish)
        {
            zmq.poll();
        }
    }
    
}

int main(int argc, char* argv[])
{
    if(argc == 2)
        test = std::stoi(argv[1]);

    std::cout << "Running test type (0 = interthread, 1 = interprocess): " << test << std::endl;
    
    goby::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test6");
    //    cfg.set_transport(goby::protobuf::InterProcessPortalConfig::TCP);
    // cfg.set_ipv4_address("127.0.0.1");
    //cfg.set_tcp_port(10005);
    cfg.set_send_queue_size(max_publish);
    cfg.set_receive_queue_size(max_publish);

    pid_t child_pid = 0;
    bool is_child = false;
    if(test == 1)
    {
        child_pid = fork();
        is_child = (child_pid == 0);
    }
    
    // goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);

    //std::string os_name = std::string("/tmp/goby_test_middleware4_") + (is_child ? "subscriber" : "publisher");
    //std::ofstream os(os_name.c_str());
    //goby::glog.add_stream(goby::common::logger::DEBUG3, &os);
    //goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    //goby::glog.set_lock_action(goby::common::logger_lock::lock);                        

    std::unique_ptr<std::thread> t10, t11;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;
    if(!is_child)
    {
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::ZMQRouter router(*router_context, cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, cfg, router);
        t11.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        std::thread t1([&] { publisher(cfg); });
        int wstatus = 0;
        if(test == 0)
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
        router_context.reset();
        manager_context.reset();
        t10->join();
        t11->join();
        if(wstatus != 0) exit(EXIT_FAILURE);
    }
    else
    {
        std::thread t1([&] { subscriber(cfg); });
        t1.join();
    }

    //    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
