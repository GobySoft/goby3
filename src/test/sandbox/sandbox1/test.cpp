#include <deque>

// plugin new serialization/parse scheme
#include "test-scheme.h"

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "test.pb.h"

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::common::logger_lock::lock);

    goby::protobuf::ZMQTransporterConfig zmq_cfg;
    zmq_cfg.set_platform("test1");
    std::unique_ptr<zmq::context_t> manager_context(new zmq::context_t(1));
    std::unique_ptr<zmq::context_t> router_context(new zmq::context_t(1));
    goby::ZMQRouter router(*router_context, zmq_cfg);
    std::thread t1([&] { router.run(); });
    goby::ZMQManager manager(*manager_context, zmq_cfg, router);
    std::thread t2([&] { manager.run(); });
    
    //    goby::ProtobufMarshaller pb;
    goby::InterThreadTransporter inproc;
    goby::ZMQTransporter<> zmq_blank(zmq_cfg);
    goby::InterProcessTransporter<goby::InterThreadTransporter> interprocess_default(inproc);
    goby::ZMQTransporter<goby::InterThreadTransporter> zmq(inproc, zmq_cfg);

    
    CTDSample s;
    s.set_salinity(38.5);

    std::cout << "Should be DCCL" << std::endl;
    zmq_blank.publish(s, "CTD");

    std::shared_ptr<CTDSample> sp(new CTDSample);
    sp->set_salinity(40.1);

    std::cout << "Should NOT be DCCL" << std::endl;
    zmq.publish<CTDSample, goby::MarshallingScheme::PROTOBUF>(sp, "CTD2");

    std::cout << "Should NOT be DCCL" << std::endl;
    TempSample t;
    t.set_temperature(15);
    zmq.publish(t, "TEMP");
 
    std::string value("HI");
    zmq.publish(value, "GroupHi");

    std::deque<char> dc = { 'H', 'E', 'L', 'L', 'O' };

    zmq.publish(dc, "GroupChar");
    
    inproc.publish(sp, "CTD3");

    
    goby::protobuf::SlowLinkTransporterConfig slow_cfg;
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
        goby::acomms::protobuf::QueueManagerConfig& queue_cfg = *slow_cfg.mutable_queue_cfg();
        queue_cfg.set_modem_id(1);
        goby::acomms::protobuf::QueuedMessageEntry& ctd_entry = *queue_cfg.add_message_entry();
        ctd_entry.set_protobuf_name("CTDSample");
        
    }
    
    
    goby::SlowLinkTransporter<decltype(zmq)> slow(zmq, slow_cfg);
    slow.publish(s, "CTD4");


    router_context.reset();
    manager_context.reset();
    t1.join();
    t2.join();

    std::cout << "all tests passed" << std::endl;
}

