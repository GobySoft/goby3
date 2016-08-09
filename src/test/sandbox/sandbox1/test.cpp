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

    goby::protobuf::ZMQTransporterConfig cfg;
    cfg.set_node("test1");
    std::unique_ptr<zmq::context_t> manager_context(new zmq::context_t(1));
    std::unique_ptr<zmq::context_t> router_context(new zmq::context_t(1));
    goby::ZMQRouter router(*router_context, cfg);
    std::thread t1([&] { router.run(); });
    goby::ZMQManager manager(*manager_context, cfg, router);
    std::thread t2([&] { manager.run(); });
    
    //    goby::ProtobufMarshaller pb;
    goby::InterThreadTransporter inproc;
    goby::ZMQTransporter<> zmq_blank(cfg);
    goby::InterProcessTransporter<goby::InterThreadTransporter> interprocess_default(inproc);
    goby::ZMQTransporter<goby::InterThreadTransporter> zmq(inproc, cfg);

    
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

    goby::SlowLinkTransporter<decltype(zmq)> slow(zmq);
    slow.publish(s, "CTD4");


    router_context.reset();
    manager_context.reset();
    t1.join();
    t2.join();

    std::cout << "all tests passed" << std::endl;
}

