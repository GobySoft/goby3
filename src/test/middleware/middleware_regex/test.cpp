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
void handle_all(const std::vector<unsigned char>& data, int scheme, const std::string& type, const goby::Group& group)
{
    glog.is(DEBUG1) && glog <<  "InterProcessForwarder received publication of " << data.size() << " bytes from group: " << group << " of type: " << type << " from scheme: " << scheme << std::endl;
    ++ipc_receive_count;
}

void subscriber()
{
    goby::InterProcessForwarder<goby::InterThreadTransporter> ipc(inproc1);
    ipc.subscribe_regex(&handle_all, {goby::MarshallingScheme::ALL_SCHEMES});
    
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while(ipc_receive_count < 3*max_publish)
    {
        ipc.poll(std::chrono::seconds(1));
        if(std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog <<  "InterProcessForwarder timed out waiting for data" << std::endl;
    }
}


// thread 3
void zmq_forward(const goby::protobuf::InterProcessPortalConfig& cfg)
{
    goby::InterProcessPortal<goby::InterThreadTransporter> ipc(inproc3, cfg);
    ipc.subscribe_regex(
        [&](const std::vector<unsigned char>& data,
            int scheme,
            const std::string& type,
            const goby::Group& group)
        {     glog.is(DEBUG1) && glog << "InterProcessPortal received publication of " << data.size() << " bytes from group: " << group << " of type: " << type << " from scheme: " << scheme << std::endl;
            assert(type == "Sample");
            assert(scheme == goby::MarshallingScheme::PROTOBUF);
 },
        {goby::MarshallingScheme::PROTOBUF},
        "Sample",
        "Sample1|Sample2"
        );
    
    zmq_ready = true;
    while(forward)
    {
        ipc.poll(std::chrono::milliseconds(100));
    }
}


int main(int argc, char* argv[])
{
    goby::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("test4");
    
    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);

    //    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);

    std::string os_name = std::string("/tmp/goby_test_middleware_regex_") + (is_child ? "subscriber" : "publisher");
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::common::logger::DEBUG3, &os);
    goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    goby::glog.set_lock_action(goby::common::logger_lock::lock);
                        


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
        forward = false;
        t3.join();

    }

    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
