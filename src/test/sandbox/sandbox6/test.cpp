#include <sys/types.h>
#include <sys/wait.h>

#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "test.pb.h"

#include <zmq.hpp>

// speed test for interprocess

int publish_count = 0;
const int max_publish = 1000000;
int ipc_receive_count = {0};

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);

using goby::glog;
using namespace goby::common::logger;

// parent process - thread 1
void publisher(const goby::protobuf::InterProcessPortalConfig& cfg)
{
    goby::InterProcessPortal<> zmq(cfg);
    sleep(1);
    
    int a = 0;
    std::cout << "Start: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;

    std::string group("Sample1");
    while(publish_count < max_publish)
    {
        Sample s;
        s.set_temperature(a++);
        s.set_salinity(30.1);
        s.set_depth(5.2);

        zmq.publish(s, group);
        ++publish_count;
        //        usleep(1e3);
    }    
    std::cout << "Publish end: " << std::setprecision(15) <<goby::common::goby_time<double>() << std::endl;

    while(forward)
    {
        zmq.poll(std::chrono::milliseconds(100));
    }

}

// child process
void handle_sample1(const Sample& sample)
{
    //std::cout << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
    
    //    if((ipc_receive_count % 100000) == 0)
    //    std::cout << ipc_receive_count << std::endl;
    
    if(ipc_receive_count == max_publish)
        std::cout << "End: " << std::setprecision(15) << goby::common::goby_time<double>() << std::endl;

}

void subscriber(const goby::protobuf::InterProcessPortalConfig& cfg)
{
    goby::InterProcessPortal<> zmq(cfg);
    zmq.subscribe<Sample>(&handle_sample1, "Sample1");
    std::cout << "Subscribed. " << std::endl;
    while(ipc_receive_count < max_publish)
    {
        zmq.poll();
    }
    
}

int main(int argc, char* argv[])
{
    goby::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test6");
    //    cfg.set_transport(goby::protobuf::InterProcessPortalConfig::TCP);
    // cfg.set_ipv4_address("127.0.0.1");
    //cfg.set_tcp_port(10005);
    cfg.set_send_queue_size(max_publish);
    cfg.set_receive_queue_size(max_publish);
    
    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);

    // goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);

    //std::string os_name = std::string("/tmp/goby_test_sandbox4_") + (is_child ? "subscriber" : "publisher");
    //std::ofstream os(os_name.c_str());
    //goby::glog.add_stream(goby::common::logger::DEBUG3, &os);
    //goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    //goby::glog.set_lock_action(goby::common::logger_lock::lock);                        

    std::unique_ptr<std::thread> t2, t3;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;
    if(!is_child)
    {
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::ZMQRouter router(*router_context, cfg);
        t2.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, cfg, router);
        t3.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        std::thread t1([&] { publisher(cfg); });
        int wstatus;
        wait(&wstatus);
        forward = false;
        t1.join();
        router_context.reset();
        manager_context.reset();
        t2->join();
        t3->join();
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
