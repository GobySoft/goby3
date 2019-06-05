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

#include <deque>

// plugin new serialization/parse scheme
#include "test-scheme.h"

#include "goby/middleware/transport.h"
#include "goby/zeromq/transport-interprocess.h"

#include "goby/util/debug_logger.h"
#include "test.pb.h"

extern constexpr goby::Group ctd{"CTD"};
extern constexpr goby::Group ctd2{"CTD2"};
extern constexpr goby::Group temp{"TEMP"};

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::common::logger_lock::lock);

    goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
    zmq_cfg.set_platform("test1");
    std::unique_ptr<zmq::context_t> manager_context(new zmq::context_t(1));
    std::unique_ptr<zmq::context_t> router_context(new zmq::context_t(1));
    goby::zeromq::Router router(*router_context, zmq_cfg);
    std::thread t1([&] { router.run(); });
    goby::zeromq::Manager manager(*manager_context, zmq_cfg, router);
    std::thread t2([&] { manager.run(); });

    //    goby::ProtobufMarshaller pb;
    goby::InterThreadTransporter inproc;
    goby::zeromq::InterProcessPortal<> zmq_blank(zmq_cfg);
    goby::InterProcessForwarder<decltype(inproc)> interprocess_default(inproc);
    goby::zeromq::InterProcessPortal<decltype(inproc)> zmq(inproc, zmq_cfg);

    CTDSample s;
    s.set_salinity(38.5);

    std::cout << "Should be DCCL" << std::endl;
    zmq_blank.publish<ctd>(s);

    std::shared_ptr<CTDSample> sp(new CTDSample);
    sp->set_salinity(40.1);

    std::cout << "Should NOT be DCCL" << std::endl;
    zmq.publish<ctd2, CTDSample, goby::MarshallingScheme::PROTOBUF>(sp);

    std::cout << "Should NOT be DCCL" << std::endl;
    TempSample t;
    t.set_temperature(15);
    zmq.publish<temp>(t);

    std::string value("HI");
    zmq.publish_dynamic(value, "GroupHi");

    std::deque<char> dc = {'H', 'E', 'L', 'L', 'O'};

    zmq.publish_dynamic(dc, "GroupChar");

    inproc.publish_dynamic(sp, "CTD3");

    goby::protobuf::InterVehiclePortalConfig slow_cfg;
    {
        slow_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_UDP);
        goby::acomms::protobuf::DriverConfig& driver_cfg = *slow_cfg.mutable_driver_cfg();
        driver_cfg.set_modem_id(1);
        UDPDriverConfig::EndPoint* local_endpoint =
            driver_cfg.MutableExtension(UDPDriverConfig::local);
        local_endpoint->set_port(11145);
        goby::acomms::protobuf::MACConfig& mac_cfg = *slow_cfg.mutable_mac_cfg();
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

    goby::InterVehiclePortal<decltype(zmq)> slow(zmq, slow_cfg);
    int slow_group = 0;
    slow.publish_dynamic(s, slow_group);

    router_context.reset();
    manager_context.reset();
    t1.join();
    t2.join();

    std::cout << "all tests passed" << std::endl;
}
