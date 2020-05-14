// Copyright 2019:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <deque>

#include "goby/middleware/marshalling/cstr.h"
#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/marshalling/protobuf.h"

// plugin new serialization/parse scheme
#include "test-scheme.h"

#include "goby/middleware/transport/interthread.h"
#include "goby/middleware/transport/intervehicle.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/util/debug_logger.h"
#include "test.pb.h"

using goby::test::zeromq::protobuf::CTDSample;
using goby::test::zeromq::protobuf::TempSample;

extern constexpr goby::middleware::Group ctd{"CTD"};
extern constexpr goby::middleware::Group ctd2{"CTD2"};
extern constexpr goby::middleware::Group temp{"TEMP"};

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
    zmq_cfg.set_platform("test1");
    std::unique_ptr<zmq::context_t> manager_context(new zmq::context_t(1));
    std::unique_ptr<zmq::context_t> router_context(new zmq::context_t(1));
    goby::zeromq::Router router(*router_context, zmq_cfg);
    std::thread t1([&] { router.run(); });
    goby::zeromq::Manager manager(*manager_context, zmq_cfg, router);
    std::thread t2([&] { manager.run(); });

    //    goby::ProtobufMarshaller pb;
    goby::middleware::InterThreadTransporter inproc;
    goby::zeromq::InterProcessPortal<> zmq_blank(zmq_cfg);
    goby::middleware::InterProcessForwarder<decltype(inproc)> interprocess_default(inproc);
    goby::zeromq::InterProcessPortal<decltype(inproc)> zmq(inproc, zmq_cfg);

    CTDSample s;
    s.set_salinity(38.5);

    std::cout << "Should be DCCL" << std::endl;
    assert(goby::middleware::scheme<decltype(s)>() == goby::middleware::MarshallingScheme::DCCL);
    // Interprocess defaults to PROTOBUF for DCCL types
    assert(zmq_blank.scheme<decltype(s)>() == goby::middleware::MarshallingScheme::PROTOBUF);
    zmq_blank.publish<ctd>(s);

    std::shared_ptr<CTDSample> sp(new CTDSample);
    sp->set_salinity(40.1);

    std::cout << "Should NOT be DCCL" << std::endl;
    zmq.publish<ctd2, CTDSample, goby::middleware::MarshallingScheme::PROTOBUF>(sp);

    std::cout << "Should NOT be DCCL" << std::endl;
    TempSample t;
    t.set_temperature(15);
    zmq.publish<temp>(t);

    // CSTR
    std::string value("HI");
    zmq.publish_dynamic(value, "GroupHi");
    zmq.publish<temp>(std::string("15"));

    std::deque<char> dc = {'H', 'E', 'L', 'L', 'O'};

    zmq.publish_dynamic(dc, "GroupChar");

    inproc.publish_dynamic(sp, "CTD3");

    goby::middleware::intervehicle::protobuf::PortalConfig slow_cfg;
    {
        auto& link_cfg = *slow_cfg.add_link();

        goby::acomms::protobuf::DriverConfig& driver_cfg = *link_cfg.mutable_driver();
        driver_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_UDP);
        driver_cfg.set_modem_id(1);
        auto* local_endpoint =
            driver_cfg.MutableExtension(goby::acomms::udp::protobuf::config)->mutable_local();
        local_endpoint->set_port(11145);
        goby::acomms::protobuf::MACConfig& mac_cfg = *link_cfg.mutable_mac();
        mac_cfg.set_modem_id(1);
        mac_cfg.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);
        goby::acomms::protobuf::ModemTransmission& slot = *mac_cfg.add_slot();
        slot.set_src(1);
        slot.set_slot_seconds(1);
        //        goby::acomms::protobuf::QueueManagerConfig& queue_cfg = *slow_cfg.mutable_queue_cfg();
        //        queue_cfg.set_modem_id(1);
        //        goby::acomms::protobuf::QueuedMessageEntry& ctd_entry = *queue_cfg.add_message_entry();
        //ctd_entry.set_protobuf_name("CTDSample");
    }

    goby::middleware::InterVehiclePortal<decltype(zmq)> slow(zmq, slow_cfg);
    slow.publish_dynamic(s, {"slow", 1});

    router_context.reset();
    manager_context.reset();
    t1.join();
    t2.join();

    std::cout << "all tests passed" << std::endl;
}
