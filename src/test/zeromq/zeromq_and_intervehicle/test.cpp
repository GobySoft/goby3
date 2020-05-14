// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Ryan Govostes <rgovostes+git@gmail.com>
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

#include <atomic>
#include <deque>

#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/transport/intervehicle.h"
#include "goby/util/debug_logger.h"
#include "goby/zeromq/transport/interprocess.h"

#include "test.pb.h"

using namespace goby::test::zeromq::protobuf;

// tests InterVehiclePortal with InterProcessPortal

int publish_count = 0;
const int max_publish = 100;
std::array<int, 3> ipc_receive_count = {{0, 0, 0}};

std::array<int, 2> direct_ack_receive_count = {{0, 0}};
int indirect_ack_receive_count = 0;
std::array<int, 2> direct_no_sub_receive_count = {{0, 0}};

int indirect_subscriber_ack = 0;
int direct_subscriber_ack = 0;

std::atomic<bool> forward(true);
std::atomic<int> zmq_reqs(0);

using goby::glog;
using namespace goby::util::logger;

constexpr goby::middleware::Group group1{"group1", 1};
constexpr goby::middleware::Group group2{"group2", 2};
constexpr goby::middleware::Group group3{"group3", 3};

constexpr goby::middleware::Group null{"broadcast_group", goby::middleware::Group::broadcast_group};

// process 1
void direct_publisher(const goby::zeromq::protobuf::InterProcessPortalConfig& zmq_cfg,
                      const goby::middleware::intervehicle::protobuf::PortalConfig& slow_cfg)
{
    goby::zeromq::InterProcessPortal<goby::middleware::InterThreadTransporter> zmq(zmq_cfg);
    goby::middleware::InterVehiclePortal<decltype(zmq)> slt(zmq, slow_cfg);

    // give time for the subscriptions to come across
    for (int i = 0; i < 20; ++i) slt.poll(std::chrono::milliseconds(100));

    double a = 0;
    while (publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a - 10);

        goby::middleware::protobuf::TransporterConfig sample_publisher_cfg;
        auto* sample_buffer_cfg = sample_publisher_cfg.mutable_intervehicle()->mutable_buffer();
        sample_buffer_cfg->set_newest_first(false);
        sample_buffer_cfg->set_ack_required(true);

        auto ack_callback = [&](const Sample& s,
                                const goby::middleware::intervehicle::protobuf::AckData& ack) {
            glog.is_debug1() && glog << "Ack for " << s.ShortDebugString()
                                     << ", ack msg: " << ack.ShortDebugString() << std::endl;
            ++direct_ack_receive_count[s.group() - 1];
        };

        auto expire_callback = [&](const Sample& s,
                                   const goby::middleware::intervehicle::protobuf::ExpireData&
                                       expire) {
            glog.is_debug1() && glog << "Expire for " << s.ShortDebugString()
                                     << ", expire msg: " << expire.ShortDebugString() << std::endl;

            switch (expire.reason())
            {
                case goby::middleware::intervehicle::protobuf::ExpireData::EXPIRED_NO_SUBSCRIBERS:
                    ++direct_no_sub_receive_count[s.group() - 1];
                    break;

                default: assert(false); break;
            }
        };

        goby::middleware::Publisher<Sample> sample_publisher(
            sample_publisher_cfg,
            [](Sample& s, const goby::middleware::Group& g) { s.set_group(g.numeric()); },
            ack_callback, expire_callback);

        slt.publish<group1>(s1, sample_publisher);
        glog.is(DEBUG1) && glog << "Published group1: " << s1->ShortDebugString() << std::endl;

        auto s2 = std::make_shared<Sample>();
        s2->set_a(a++);
        slt.publish<group2>(s2, sample_publisher);
        glog.is(DEBUG1) && glog << "Published group2: " << s2->ShortDebugString() << std::endl;

        goby::middleware::protobuf::TransporterConfig widget_publisher_cfg;
        auto* widget_buffer_cfg = widget_publisher_cfg.mutable_intervehicle()->mutable_buffer();
        widget_buffer_cfg->set_newest_first(false);
        widget_buffer_cfg->set_ack_required(false);

        Widget w;
        w.set_b(a - 2);
        slt.publish<null>(w, {widget_publisher_cfg});

        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        usleep(1e3);
        ++publish_count;
    }

    while (forward) { slt.poll(std::chrono::milliseconds(100)); }

    // no subscriber
    assert(direct_ack_receive_count[0] == 0);
    assert(direct_no_sub_receive_count[0] == max_publish);

    // one subscriber
    assert(direct_ack_receive_count[1] == max_publish);
    assert(direct_no_sub_receive_count[1] == 0);
}

// process 2
void indirect_publisher(const goby::zeromq::protobuf::InterProcessPortalConfig& zmq_cfg)
{
    goby::zeromq::InterProcessPortal<> zmq(zmq_cfg);
    goby::middleware::InterVehicleForwarder<decltype(zmq)> interplatform(zmq);
    double a = 0;
    while (publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++ - 10);

        goby::middleware::protobuf::TransporterConfig sample_publisher_cfg;
        auto* sample_buffer_cfg = sample_publisher_cfg.mutable_intervehicle()->mutable_buffer();
        sample_buffer_cfg->set_newest_first(false);
        sample_buffer_cfg->set_ack_required(true);

        auto ack_callback = [&](const Sample& s,
                                const goby::middleware::intervehicle::protobuf::AckData& ack) {
            glog.is_debug1() && glog << "Ack for " << s.ShortDebugString()
                                     << ", ack msg: " << ack.ShortDebugString() << std::endl;
            ++indirect_ack_receive_count;
        };

        auto expire_callback =
            [&](const Sample& s,
                const goby::middleware::intervehicle::protobuf::ExpireData& expire) {
                glog.is_debug1() && glog << "Expire for " << s.ShortDebugString()
                                         << ", expire msg: " << expire.ShortDebugString()
                                         << std::endl;

                switch (expire.reason())
                {
                    default: assert(false); break;
                }
            };

        goby::middleware::Publisher<Sample> sample_publisher(
            sample_publisher_cfg,
            [](Sample& s, const goby::middleware::Group& g) { s.set_group(g.numeric()); },
            ack_callback, expire_callback);
        interplatform.publish<group3>(s1, sample_publisher);

        glog.is(DEBUG1) && glog << "Published: " << publish_count << std::endl;
        usleep(1e3);
        ++publish_count;
    }

    while (indirect_ack_receive_count != max_publish || forward)
    { interplatform.poll(std::chrono::milliseconds(100)); } }

// process 3
void handle_sample1(const Sample& sample)
{
    glog.is(DEBUG1) && glog << "InterVehiclePortal received publication sample1: "
                            << sample.ShortDebugString() << std::endl;
    assert(sample.a() == ipc_receive_count[0]);
    ++ipc_receive_count[0];
}

void handle_sample_indirect(const Sample& sample)
{
    glog.is(DEBUG1) && glog << "InterVehiclePortal received indirect sample: "
                            << sample.ShortDebugString() << std::endl;
    assert(sample.a() == ipc_receive_count[1] - 10);
    ++ipc_receive_count[1];
}

void handle_widget(std::shared_ptr<const Widget> w)
{
    glog.is(DEBUG1) && glog << "InterVehiclePortal received publication widget: "
                            << w->ShortDebugString() << std::endl;
    assert(w->b() == ipc_receive_count[2] - 1);
    ++ipc_receive_count[2];
}

void direct_subscriber(const goby::zeromq::protobuf::InterProcessPortalConfig& zmq_cfg,
                       const goby::middleware::intervehicle::protobuf::PortalConfig& slow_cfg)
{
    goby::zeromq::InterProcessPortal<goby::middleware::InterThreadTransporter> zmq(zmq_cfg);
    goby::middleware::InterVehiclePortal<decltype(zmq)> slt(zmq, slow_cfg);

    goby::middleware::protobuf::TransporterConfig sample_subscriber_cfg;
    sample_subscriber_cfg.mutable_intervehicle()->add_publisher_id(1);

    using goby::middleware::intervehicle::protobuf::Subscription;
    auto ack_callback = [&](const Subscription& s,
                            const goby::middleware::intervehicle::protobuf::AckData& ack) {
        glog.is_debug1() && glog << "Subscription Ack for " << s.ShortDebugString()
                                 << ", ack msg: " << ack.ShortDebugString() << std::endl;
        ++direct_subscriber_ack;
    };

    auto expire_callback = [&](const Subscription& s,
                               const goby::middleware::intervehicle::protobuf::ExpireData& expire) {
        glog.is_debug1() && glog << "Subscription Expire for " << s.ShortDebugString()
                                 << ", expire msg: " << expire.ShortDebugString() << std::endl;
        assert(false);
    };

    goby::middleware::Subscriber<Sample> sample_subscriber(
        sample_subscriber_cfg, [](const Sample& s) { return s.group(); }, ack_callback,
        expire_callback);

    slt.subscribe<group2, Sample>(&handle_sample1, sample_subscriber);
    slt.subscribe<group3, Sample>(&handle_sample_indirect, sample_subscriber);

    goby::middleware::protobuf::TransporterConfig widget_subscriber_cfg;
    widget_subscriber_cfg.mutable_intervehicle()->add_publisher_id(1);
    goby::middleware::Subscriber<Widget> widget_subscriber(widget_subscriber_cfg);
    slt.subscribe<null, Widget>(&handle_widget, widget_subscriber_cfg);

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while (ipc_receive_count[0] < max_publish || ipc_receive_count[1] < max_publish ||
           ipc_receive_count[2] < max_publish)
    {
        slt.poll(std::chrono::seconds(1));
        if (std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog << "InterVehiclePortal timed out waiting for data" << std::endl;
    }

    assert(direct_subscriber_ack == 2);
}

// process 4

void indirect_handle_sample_indirect(const Sample& sample)
{
    glog.is(DEBUG1) && glog << "InterVehicleForwarder received indirect sample: "
                            << sample.ShortDebugString() << std::endl;
    assert(sample.a() == ipc_receive_count[0] - 10);
    ++ipc_receive_count[0];
}

void indirect_subscriber(const goby::zeromq::protobuf::InterProcessPortalConfig& zmq_cfg)
{
    goby::zeromq::InterProcessPortal<> zmq(zmq_cfg);
    goby::middleware::InterVehicleForwarder<decltype(zmq)> interplatform(zmq);

    goby::middleware::protobuf::TransporterConfig sample_indirect_subscriber_cfg;
    sample_indirect_subscriber_cfg.mutable_intervehicle()->add_publisher_id(1);

    using goby::middleware::intervehicle::protobuf::Subscription;
    auto ack_callback = [&](const Subscription& s,
                            const goby::middleware::intervehicle::protobuf::AckData& ack) {
        glog.is_debug1() && glog << "Subscription Ack for " << s.ShortDebugString()
                                 << ", ack msg: " << ack.ShortDebugString() << std::endl;
        ++indirect_subscriber_ack;
    };

    auto expire_callback = [&](const Subscription& s,
                               const goby::middleware::intervehicle::protobuf::ExpireData& expire) {
        glog.is_debug1() && glog << "Subscription Expire for " << s.ShortDebugString()
                                 << ", expire msg: " << expire.ShortDebugString() << std::endl;
        assert(false);
    };

    interplatform.subscribe_dynamic<Sample>(
        &indirect_handle_sample_indirect, 3,
        goby::middleware::Subscriber<Sample>(sample_indirect_subscriber_cfg,
                                             [](const Sample& s) { return s.group(); },
                                             ack_callback, expire_callback));

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point timeout = start + std::chrono::seconds(10);
    while (ipc_receive_count[0] < max_publish)
    {
        interplatform.poll(std::chrono::seconds(1));
        if (std::chrono::system_clock::now() > timeout)
            glog.is(DIE) && glog << "InterVehicleTransport timed out waiting for data" << std::endl;
    }

    assert(indirect_subscriber_ack == 1);
}

int main(int argc, char* argv[])
{
    int process_index = 0;
    const int number_children = 3;
    for (int i = 1; i <= number_children; ++i)
    {
        pid_t child_pid = fork();
        if (child_pid == 0)
        {
            process_index = i;
            break;
        }
    }

    // goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    std::string process_suffix =
        ((process_index >= 2) ? ("subscriber_" + std::to_string(process_index))
                              : ("publisher_" + std::to_string(process_index)));
    std::string os_name = std::string("/tmp/goby_test_intervehicle_") + process_suffix;
    std::ofstream os(os_name.c_str());
    goby::glog.add_stream(goby::util::logger::DEBUG3, &os);
    //    dccl::dlog.connect(dccl::logger::ALL, &os, true);
    goby::glog.set_name(std::string(argv[0]) + process_suffix);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    std::unique_ptr<std::thread> t10, t11;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;

    goby::middleware::intervehicle::protobuf::PortalConfig slow_cfg;

    auto& link_cfg = *slow_cfg.add_link();
    goby::acomms::protobuf::DriverConfig& driver_cfg = *link_cfg.mutable_driver();
    driver_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_UDP_MULTICAST);
    auto* udp_multicast_driver_cfg =
        driver_cfg.MutableExtension(goby::acomms::udp_multicast::protobuf::config);
    udp_multicast_driver_cfg->set_max_frame_size(64);
    udp_multicast_driver_cfg->set_multicast_port(60000);

    goby::acomms::protobuf::MACConfig& mac_cfg = *link_cfg.mutable_mac();
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

    if (process_index == 0)
    {
        link_cfg.set_modem_id(1);
        slot.set_src(1);
        //queue_cfg.set_modem_id(1);

        goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle1");

        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::zeromq::Router router(*router_context, zmq_cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::zeromq::Manager manager(*manager_context, zmq_cfg, router);
        t11.reset(new std::thread([&] { manager.run(); }));
        sleep(1);

        std::thread t1([&] { direct_publisher(zmq_cfg, slow_cfg); });
        std::array<int, number_children> wstatus;
        for (int i = 0; i < number_children; ++i) wait(&wstatus[i]);

        forward = false;
        t1.join();
        router_context.reset();
        manager_context.reset();
        t10->join();
        t11->join();
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
        for (int ws : wstatus)
        {
            if (ws != 0)
            {
                std::cout << "Test failed (see logs in /tmp)" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
    }
    else if (process_index == 1)
    {
        goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle1");

        // wait for ZMQ (process_index == 0) to start up
        sleep(3);
        std::thread t1([&] { indirect_publisher(zmq_cfg); });
        forward = false;
        t1.join();
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
    }
    else if (process_index == 2)
    {
        link_cfg.set_modem_id(2);
        slot.set_src(2);
        //queue_cfg.set_modem_id(2);

        goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle2");

        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));

        goby::zeromq::Router router(*router_context, zmq_cfg);
        t10.reset(new std::thread([&] { router.run(); }));
        goby::zeromq::Manager manager(*manager_context, zmq_cfg, router);
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
    else if (process_index == 3)
    {
        sleep(3);
        goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
        zmq_cfg.set_platform("test5-vehicle2");
        std::thread t1([&] { indirect_subscriber(zmq_cfg); });
        t1.join();
        glog.is(VERBOSE) && glog << process_suffix << ": all tests passed" << std::endl;
    }

    std::cout << process_suffix << ": all tests passed" << std::endl;

    dccl::DynamicProtobufManager::protobuf_shutdown();
}
