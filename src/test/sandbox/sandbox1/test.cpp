#include <deque>

// plugin new serialization/parse scheme
#include "test-scheme.h"

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "goby/sandbox/marshalling.h"
#include "test.pb.h"

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    //    goby::ProtobufMarshaller pb;
    goby::ZMQTransporter<> zmq_blank;
    goby::ZMQTransporter<goby::IntraProcessTransporter> zmq_default;

    goby::IntraProcessTransporter inproc;
    goby::ZMQTransporter<goby::IntraProcessTransporter> zmq(inproc);
    
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
    
    std::cout << "all tests passed" << std::endl;
}

