#include <sys/types.h>
#include <sys/wait.h>

#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "test.pb.h"

#include <zmq.hpp>

// tests InterProcessTransporter

goby::InterThreadTransporter inproc;

int publish_count = 0;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<int> ready(0);
std::atomic<bool> forward(true);
std::atomic<bool> zmq_ready(false);

using goby::glog;
using namespace goby::common::logger;


// thread 1 - parent process
void publisher()
{
    goby::InterProcessTransporter<goby::InterThreadTransporter> ipc(inproc);
    double a = 0;
    while(publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        ipc.publish(s1, "Sample1");
        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        ipc.publish(s2, "Sample2");
        auto w1 = std::make_shared<Widget>();
        w1->set_b(s1->a() - 8);
        ipc.publish(w1, "Widget");
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


void handle_widget(const Widget& widget)
{
    glog.is(DEBUG1) && glog <<  "InterProcess received publication: " << widget.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void subscriber()
{
    goby::InterProcessTransporter<goby::InterThreadTransporter> ipc(inproc);
    ipc.subscribe<Sample>("Sample1", &handle_sample1);
    ipc.subscribe<Sample>("Sample2", &handle_sample2);
    ipc.subscribe<Widget>("Widget", &handle_widget);
    while(ipc_receive_count < 3*max_publish)
        ipc.poll();
}




// thread(s) 2

class ThreadSubscriber
{
public:
    void run()
        {
            // subscribe using lambda capture
            inproc.subscribe<Sample>("Sample1", [&](const Sample& s) { handle_sample1(s); });
            // subscribe using overload for member functions
            inproc.subscribe<Sample>("Sample2", &ThreadSubscriber::handle_sample2, this);
            inproc.subscribe("Widget", &ThreadSubscriber::handle_widget1, this);
            ++ready;
            while(receive_count1 < max_publish || receive_count2 < max_publish || receive_count3 < max_publish)
            {
                inproc.poll();
                //  std::cout << "Polled " << items  << " items. " << std::endl;
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
void zmq_forward(const goby::protobuf::ZMQTransporterConfig& cfg)
{
    goby::ZMQTransporter<goby::InterThreadTransporter> zmq(inproc, cfg);
    zmq_ready = true;
    while(forward)
    {
        zmq.poll(std::chrono::milliseconds(100));
    }
}


int main(int argc, char* argv[])
{
    goby::protobuf::ZMQTransporterConfig cfg;
    cfg.set_node("test4");
    
    pid_t child_pid = fork();

    bool is_child = (child_pid == 0);

    //    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);

    std::string os_name = std::string("/tmp/goby_test_sandbox3_") + (is_child ? "subscriber" : "publisher");
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
