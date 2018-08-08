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

#include <sys/types.h>
#include <sys/wait.h>

#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/middleware/transport.h"
#include "test.pb.h"

#include <zmq.hpp>

// tests InterVehiclePortal with InterProcessPortal

int publish_count = 0;
const int max_publish = 100;
std::array<int, 3> ipc_receive_count = {{0, 0, 0}};

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);

using goby::glog;
using namespace goby::common::logger;

// process 1
void direct_publisher(const goby::protobuf::InterProcessPortalConfig& zmq_cfg, const goby::protobuf::InterVehiclePortalConfig& slow_cfg)
{
    goby::InterProcessPortal<> zmq(zmq_cfg);
    goby::InterVehiclePortal<decltype(zmq)> slt(zmq, slow_cfg);

    double a = 0;
    while(publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a-10);
        s1->set_group(1);
        slt.publish_dynamic(s1, s1->group());

        auto s2 = std::make_shared<Sample>();
        s2->set_a(a++);
        s2->set_group(2);
        slt.publish_dynamic(s2, s2->group());

        Widget w;
        w.set_b(a-2);
        slt.publish_no_group(w);
            
        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        usleep(1e3);
        ++publish_count;
    }    

    while(forward)
    {
        slt.poll(std::chrono::milliseconds(100));
    }
}

// process 2
void indirect_publisher(const goby::protobuf::InterProcessPortalConfig& zmq_cfg)
{
    goby::InterProcessPortal<> zmq(zmq_cfg);
    goby::InterVehicleForwarder<decltype(zmq)> interplatform(zmq);
    double a = 0;
    while(publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++-10);
        s1->set_group(3);
        interplatform.publish_dynamic(s1, s1->group());
            
        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        usleep(1e3);
        ++publish_count;
    }    

    while(forward)
    {
        interplatform.poll(std::chrono::milliseconds(100));
    }
}



// process 3
void handle_sample1(const Sample& sample)
{
    glog.is(DEBUG1) && glog <<  "InterVehiclePortal received publication sample1: " << sample.ShortDebugString() << std::endl;
    assert(sample.a() == ipc_receive_count[0]);
    ++ipc_receive_count[0];
}

void handle_sample_indirect(const Sample& sample)
{
    glog.is(DEBUG1) && glog <<  "InterVehiclePortal received indirect sample: " << sample.ShortDebugString() << std::endl;
    assert(sample.a() == ipc_receive_count[1]-10);
    ++ipc_receive_count[1];
}

void handle_widget(std::shared_ptr<const Widget> w)
{
    glog.is(DEBUG1) && glog <<  "InterVehiclePortal received publication widget: " << w->ShortDebugString() << std::endl;
    assert(w->b() == ipc_receive_count[2]-1);
    ++ipc_receive_count[2];
}

void direct_subscriber(const goby::protobuf::InterProcessPortalConfig& zmq_cfg, const goby::protobuf::InterVehiclePortalConfig& slow_cfg)
{
    goby::InterProcessPortal<> zmq(zmq_cfg);
    goby::InterVehiclePortal<decltype(zmq)> slt(zmq, slow_cfg);
    
    slt.subscribe_dynamic<Sample>(&handle_sample1, 2, [](const Sample& s) { return s.group(); });
    slt.subscribe_dynamic<Sample>(&handle_sample_indirect, 3, [](const Sample& s) { return s.group(); });
    slt.subscribe_no_group<Widget>(&handle_widget);

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while(ipc_receive_count[0] < max_publish || ipc_receive_count[1] < max_publish || ipc_receive_count[2] < max_publish)
    {
        slt.poll(std::chrono::seconds(1));
        if(std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog <<  "InterVehiclePortal timed out waiting for data" << std::endl;
    }

}

// process 4


void indirect_handle_sample_indirect(const Sample& sample)
{
    glog.is(DEBUG1) && glog <<  "InterVehicleForwarder received indirect sample: " << sample.ShortDebugString() << std::endl;
    assert(sample.a() == ipc_receive_count[0]-10);
    ++ipc_receive_count[0];
}

void indirect_subscriber(const goby::protobuf::InterProcessPortalConfig& zmq_cfg)
{
    goby::InterProcessPortal<> zmq(zmq_cfg);
    goby::InterVehicleForwarder<decltype(zmq)> interplatform(zmq);
    interplatform.subscribe_dynamic<Sample>(&indirect_handle_sample_indirect, 3, [](const Sample& s) { return s.group(); });

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while(ipc_receive_count[0] < max_publish)
    {
        interplatform.poll(std::chrono::seconds(1));
        if(std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog <<  "InterVehicleTransport timed out waiting for data" << std::endl;
    }

}


int main(int argc, char* argv[])
{
    int process_index = 0;
    const int number_children = 3;
    for(int i = 1; i <= number_children; ++i)
    {
        pid_t child_pid = fork();
        if(child_pid == 0)
        {
            process_index = i;
            break;
        }
    }
        
    // goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    std::string process_suffix =  ((process_index >= 2) ? ("subscriber_" + std::to_string(process_index)) : ("publisher_" + std::to_string(process_index)));
    std::string os_name = std::string("/tmp/goby_test_middleware5_") + process_suffix;
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::common::logger::DEBUG3, &os);
    //    dccl::dlog.connect(dccl::logger::ALL, &os, true);
    goby::glog.set_name(std::string(argv[0])  + process_suffix);
    goby::glog.set_lock_action(goby::common::logger_lock::lock);

    std::unique_ptr<std::thread> t10, t11;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;

    goby::protobuf::InterVehiclePortalConfig slow_cfg;
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
    //    goby::acomms::protobuf::QueueManagerConfig& queue_cfg = *slow_cfg.mutable_queue_cfg();
    //    goby::acomms::protobuf::QueuedMessageEntry& sample_entry = *queue_cfg.add_message_entry();
    //sample_entry.set_protobuf_name("Sample");
    //sample_entry.set_newest_first(false);
    //sample_entry.set_max_queue(2*max_publish + 1);

    // goby::acomms::protobuf::QueuedMessageEntry& widget_entry = *queue_cfg.add_message_entry();
    // widget_entry.set_protobuf_name("Widget");
    // widget_entry.set_newest_first(false);
    // widget_entry.set_max_queue(max_publish + 1);
    
    if(process_index == 0)
    {
        driver_cfg.set_modem_id(1);
        local_endpoint->set_port(60011);
        mac_cfg.set_modem_id(1);
        slot.set_src(1);
        //queue_cfg.set_modem_id(1);
        remote_endpoint->set_ip("127.0.0.1");
        remote_endpoint->set_port(60012);
    
        
        goby::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle1");
    
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::ZMQRouter router(*router_context, zmq_cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::ZMQManager manager(*manager_context, zmq_cfg, router);
        t11.reset(new std::thread([&] { manager.run(); }));
        sleep(1);
        

        std::thread t1([&] { direct_publisher(zmq_cfg, slow_cfg); });
        std::array<int, number_children> wstatus;
        for(int i = 0; i < number_children; ++i)
            wait(&wstatus[i]);
        
        forward = false;
        t1.join();
        router_context.reset();
        manager_context.reset();
        t10->join();
        t11->join();
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
        for(int ws : wstatus)
        {
            if(ws != 0)
            {
                std::cout << "Test failed (see logs in /tmp)" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        
    }
    else if(process_index == 1)
    {
        goby::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle1");

        // wait for ZMQ (process_index == 0) to start up
        sleep(3);
        std::thread t1([&] { indirect_publisher(zmq_cfg); });
        forward = false;
        t1.join();
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
    }
    else if(process_index == 2)
    {
        driver_cfg.set_modem_id(2);
        local_endpoint->set_port(60012);
        mac_cfg.set_modem_id(2);
        slot.set_src(2);
        //queue_cfg.set_modem_id(2);
        remote_endpoint->set_ip("127.0.0.1");
        remote_endpoint->set_port(60011);

        goby::protobuf::InterProcessPortalConfig zmq_cfg;
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
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
    }
    else if(process_index == 3)
    {
        sleep(3);
        goby::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle2");
        std::thread t1([&] { indirect_subscriber(zmq_cfg); });
        t1.join();
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
    }    

    std::cout << process_suffix << ": all tests passed" << std::endl;
}
