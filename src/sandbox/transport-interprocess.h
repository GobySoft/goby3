#ifndef TransportInterProcess20160622H
#define TransportInterProcess20160622H

#include <sys/types.h>
#include <unistd.h>

#include "goby/common/zeromq_service.h"

#include "transport-common.h"
#include "goby/sandbox/protobuf/interprocess_data.pb.h"

namespace goby
{   

    enum { ZMQ_MARSHALLING_SCHEME = 0x474f4259 };
    
    
    template<typename InnerTransporter>
        class InterProcessTransporter
        {
        public:
        InterProcessTransporter(InnerTransporter& inner) : inner_(inner) { }
        ~InterProcessTransporter() { }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(const DataType& data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            _publish<DataType, scheme>(data, group, transport_cfg);
            inner_.publish<DataType, scheme>(data, group, transport_cfg);
        }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(std::shared_ptr<DataType> data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            if(data)
            {
                _publish<DataType, scheme>(*data, group, transport_cfg);
                inner_.publish<DataType, scheme>(data, group, transport_cfg);
            }
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class Function>
        void subscribe(const std::string& group, Function f)
        {
            std::function<void(const DataType&)> func(f);
            inner_.subscribe<DataType, scheme, Function>(group, f);        
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
        void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
        {
            subscribe<DataType, scheme>(group, std::bind(mem_func, c, std::placeholders::_1));
        }

        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return inner_.poll(timeout);
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            return poll(std::chrono::system_clock::now() + wait_for);
        }

        static const std::string forward_group;
        
        private:
        template<typename DataType, int scheme>
        void _publish(const DataType& d, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<DataType, scheme>::serialize(d));
            std::string* sbytes = new std::string(bytes.begin(), bytes.end());
            std::shared_ptr<goby::protobuf::InterProcessData> data = std::make_shared<goby::protobuf::InterProcessData>();

            data->set_marshalling_scheme(scheme);
            data->set_type(SerializerParserHelper<DataType, scheme>::type_name(d));
            data->set_group(group);
            data->set_allocated_data(sbytes);
        
            *data->mutable_cfg() = transport_cfg;

            //            std::cout << "InterProcessTransporter: Publishing: " << data->DebugString() << std::endl;
            inner_.publish(data, forward_group);
        }

        InnerTransporter& inner_;
        
        };


    template<typename InnerTransport>
        const std::string InterProcessTransporter<InnerTransport>::forward_group( "goby::InterProcessTransporter");

    template<typename InnerTransporter = NoOpTransporter>
        class ZMQTransporter
        {
        public:
        ZMQTransporter() : own_inner_(new InnerTransporter), inner_(*own_inner_)
        { _init(); }

        ZMQTransporter(InnerTransporter& inner) : inner_(inner)
        { _init(); }
        
        ~ZMQTransporter() { }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(const DataType& data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            _publish<DataType, scheme>(data, group, transport_cfg);
            inner_.publish<DataType, scheme>(data, group, transport_cfg);
        }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(std::shared_ptr<DataType> data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            if(data)
            {
                _publish<DataType, scheme>(*data, group, transport_cfg);
                inner_.publish<DataType, scheme>(data, group, transport_cfg);
            }
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class Function>
        void subscribe(const std::string& group, Function f)
        {
            std::function<void(const DataType&)> func(f);
            inner_.subscribe<DataType, scheme, Function>(group, f);        
            zmq_.subscribe(ZMQ_MARSHALLING_SCHEME, _make_identifier<DataType>(group), SOCKET_SUBSCRIBE);
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
        void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
        {
            subscribe<DataType, scheme>(group, std::bind(mem_func, c, std::placeholders::_1));
        }

        int poll(std::chrono::system_clock::duration wait_for)
        {
            inner_.poll(std::chrono::seconds(0));
            return zmq_.poll(std::chrono::duration_cast<std::chrono::microseconds>(wait_for).count());
        }
    
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return poll(timeout - std::chrono::system_clock::now());
        }
        
        private:
        void _init()
        {
            inner_.subscribe<goby::protobuf::InterProcessData>(InterProcessTransporter<InnerTransporter>::forward_group, &ZMQTransporter::_receive_forwarded, this);
            zmq_.connect_inbox_slot(&ZMQTransporter::_zmq_inbox, this);

            goby::common::protobuf::ZeroMQServiceConfig cfg;

            goby::common::protobuf::ZeroMQServiceConfig::Socket* subscribe_socket = cfg.add_socket();
            subscribe_socket->set_socket_type(common::protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
            subscribe_socket->set_socket_id(SOCKET_SUBSCRIBE);
            subscribe_socket->set_transport(common::protobuf::ZeroMQServiceConfig::Socket::TCP);
            subscribe_socket->set_connect_or_bind(common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);
            subscribe_socket->set_ethernet_address("127.0.0.1");
            subscribe_socket->set_ethernet_port(5555);

            goby::common::protobuf::ZeroMQServiceConfig::Socket* publish_socket = cfg.add_socket();
            publish_socket->set_socket_type(common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
            publish_socket->set_socket_id(SOCKET_PUBLISH);
            publish_socket->set_transport(common::protobuf::ZeroMQServiceConfig::Socket::TCP);
            publish_socket->set_connect_or_bind(common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);
            publish_socket->set_ethernet_address("127.0.0.1");
            publish_socket->set_ethernet_port(5556);

            zmq_.set_cfg(&cfg);
            //            std::cout << "Bound to port: " << cfg.socket(0).ethernet_port() << std::endl;
            publish_port_ = cfg.socket(0).ethernet_port();
        }
        
        void _receive_forwarded(std::shared_ptr<const goby::protobuf::InterProcessData> data)
        {
            //            std::cout << "Forwarding: " << data->DebugString() << std::endl;
        }
        
        template<typename DataType, int scheme>
        void _publish(const DataType& d, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<DataType, scheme>::serialize(d));
            std::string sbytes(bytes.begin(), bytes.end());
            zmq_.send(ZMQ_MARSHALLING_SCHEME, _make_identifier<DataType>(group), sbytes, SOCKET_PUBLISH);
        }

        void _zmq_inbox(int marshalling_scheme,
                        const std::string& identifier,
                        const std::string& body,
                        int socket_id)
        {
            std::vector<std::string> identifier_parts;
            boost::split(identifier_parts, identifier, boost::is_any_of("/"));
            std::cout << "ZMQ inbox: " << identifier << std::endl;
        }

        template<typename DataType, int scheme>
        std::string _make_identifier(const std::string& group)
        {
            std::string sscheme(std::to_string(scheme));
            std::string process(std::to_string(getpid()));
            std::string thread(std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
            
            return std::string("/" + SerializerParserHelper<DataType, scheme>::type_name(DataType()) +
                               "/" + sscheme +
                               "/" + process +
                               "/" + thread +
                               "/");
        }
        
        private:

        enum { SOCKET_MANAGER = 0, SOCKET_SUBSCRIBE = 1, SOCKET_PUBLISH = 2 };
        
        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        goby::common::ZeroMQService zmq_;
        int publish_port_ = { 0 };
        };


    

}


#endif
