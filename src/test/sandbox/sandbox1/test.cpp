#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "goby/sandbox/marshalling.h"
#include "test.pb.h"

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    goby::ProtobufMarshaller pb;
    goby::ZMQTransporter<> zmq;
    
    CTDSample s;
    s.set_salinity(38.5);
    
    pb.publish(s, "CTD", zmq);

    std::shared_ptr<CTDSample> sp(new CTDSample);
    sp->set_salinity(40.1);

    pb.publish(*sp, "CTD2", zmq);

    goby::IntraProcessTransporter inproc;

    pb.publish(sp, "CTD3", inproc);
    
    std::cout << "all tests passed" << std::endl;
}

