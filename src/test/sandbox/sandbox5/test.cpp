#include <sys/types.h>
#include <sys/wait.h>

#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "test.pb.h"

#include <zmq.hpp>

// tests SlowLinkTransporter with ZMQTransporter

int publish_count = 0;
const int max_publish = 100;
int ipc_receive_count = {0};

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);

using goby::glog;
using namespace goby::common::logger;

// parent process - thread 1
void direct_publisher(const goby::protobuf::ZMQTransporterConfig& zmq_cfg, const goby::protobuf::SlowLinkTransporterConfig& slow_cfg)
{
    goby::ZMQTransporter<> zmq(zmq_cfg);
    goby::SlowLinkTransporter<decltype(zmq)> slt(zmq, slow_cfg);

    double a = 0;
    while(publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a-10);
        s1->set_group(1);
        slt.publish(s1, s1->group());

        auto s2 = std::make_shared<Sample>();
        s2->set_a(a++);
        s2->set_group(2);
        slt.publish(s2, s2->group());

        Widget w;
        w.set_b(a-2);
        slt.publish(w);
            
        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        usleep(1e3);
        ++publish_count;
    }    

    while(forward)
    {
        slt.poll(std::chrono::milliseconds(100));
    }
}

// thread 2
void indirect_publisher(const goby::protobuf::ZMQTransporterConfig& zmq_cfg)
{
    goby::ZMQTransporter<> zmq(zmq_cfg);
    goby::InterVehicleTransporter<decltype(zmq)> intervehicle(zmq);
    double a = 0;
    while(publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a-10);
        s1->set_group(3);
        intervehicle.publish(s1, s1->group());
            
        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        usleep(1e3);
        ++publish_count;
    }    

    while(forward)
    {
        intervehicle.poll(std::chrono::milliseconds(100));
    }
}



// child process
void handle_sample1(const Sample& sample)
{
    glog.is(DEBUG1) && glog <<  "SlowLinkTransporter received publication sample1: " << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void handle_sample_indirect(const Sample& sample)
{
    glog.is(DEBUG1) && glog <<  "SlowLinkTransporter received indirect sample: " << sample.ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void handle_widget(std::shared_ptr<const Widget> w)
{
    glog.is(DEBUG1) && glog <<  "SlowLinkTransporter received publication widget: " << w->ShortDebugString() << std::endl;
    ++ipc_receive_count;
}

void direct_subscriber(const goby::protobuf::ZMQTransporterConfig& zmq_cfg, const goby::protobuf::SlowLinkTransporterConfig& slow_cfg)
{
    goby::ZMQTransporter<> zmq(zmq_cfg);
    goby::SlowLinkTransporter<decltype(zmq)> slt(zmq, slow_cfg);

    slt.subscribe<Sample>(&handle_sample1, 2, [](const Sample& s) { return s.group(); });
    slt.subscribe<Sample>(&handle_sample_indirect, 3, [](const Sample& s) { return s.group(); });
    slt.subscribe<Widget>(&handle_widget);

    while(ipc_receive_count < 2*max_publish)
    {
        slt.poll();
        std::cout << "poll" << std::endl;
    }

}

int main(int argc, char* argv[])
{
    pid_t child_pid = fork();
    
    bool is_child = (child_pid == 0);

    // goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    std::string os_name = std::string("/tmp/goby_test_sandbox5_") + (is_child ? "subscriber" : "publisher");
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::common::logger::DEBUG3, &os);
    //    dccl::dlog.connect(dccl::logger::ALL, &os, true);
    goby::glog.set_name(std::string(argv[0]) + (is_child ? "_subscriber" : "_publisher"));
    goby::glog.set_lock_action(goby::common::logger_lock::lock);                        

    std::unique_ptr<std::thread> t10, t11;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;

    goby::protobuf::SlowLinkTransporterConfig slow_cfg;
    slow_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_UDP);
    goby::acomms::protobuf::DriverConfig& driver_cfg = *slow_cfg.mutable_driver_cfg();
    UDPDriverConfig::EndPoint* local_endpoint =
        driver_cfg.MutableExtension(UDPDriverConfig::local);
    UDPDriverConfig::EndPoint* remote_endpoint =
        driver_cfg.MutableExtension(UDPDriverConfig::remote);
    driver_cfg.SetExtension(UDPDriverConfig::max_frame_size, 64);
    
    goby::acomms::protobuf::MACConfig& mac_cfg = *slow_cfg.mutable_mac_cfg();
    mac_cfg.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);
    goby::acomms::protobuf::ModemTransmission& slot = *mac_cfg.add_slot();
    slot.set_slot_seconds(0.2);
    goby::acomms::protobuf::QueueManagerConfig& queue_cfg = *slow_cfg.mutable_queue_cfg();
    goby::acomms::protobuf::QueuedMessageEntry& sample_entry = *queue_cfg.add_message_entry();
    sample_entry.set_protobuf_name("Sample");
    sample_entry.set_newest_first(false);
    sample_entry.set_max_queue(2*max_publish + 1);

    goby::acomms::protobuf::QueuedMessageEntry& widget_entry = *queue_cfg.add_message_entry();
    widget_entry.set_protobuf_name("Widget");
    widget_entry.set_newest_first(false);
    widget_entry.set_max_queue(max_publish + 1);
    
    if(!is_child)
    {
        driver_cfg.set_modem_id(1);
        local_endpoint->set_port(60011);
        mac_cfg.set_modem_id(1);
        slot.set_src(1);
        queue_cfg.set_modem_id(1);
        remote_endpoint->set_ip("127.0.0.1");
        remote_endpoint->set_port(60012);
    
        
        goby::protobuf::ZMQTransporterConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle1");
    
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::ZMQRouter router(*router_context, zmq_cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, zmq_cfg, router);
        t11.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        

        std::thread t1([&] { direct_publisher(zmq_cfg, slow_cfg); });
        sleep(2);
        std::thread t2([&] { indirect_publisher(zmq_cfg); });
        int wstatus;
        wait(&wstatus);
        
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
        driver_cfg.set_modem_id(2);
        local_endpoint->set_port(60012);
        mac_cfg.set_modem_id(2);
        slot.set_src(2);
        queue_cfg.set_modem_id(2);
        remote_endpoint->set_ip("127.0.0.1");
        remote_endpoint->set_port(60011);

        goby::protobuf::ZMQTransporterConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle2");
        
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::ZMQRouter router(*router_context, zmq_cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, zmq_cfg, router);
        t11.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        
        std::thread t1([&] { direct_subscriber(zmq_cfg, slow_cfg); });
        t1.join();
        router_context.reset();
        manager_context.reset();
        t10->join();
        t11->join();
    }

    glog.is(VERBOSE) && glog << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
    std::cout << (is_child ? "subscriber" : "publisher") << ": all tests passed" << std::endl;
}
