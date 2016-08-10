#ifndef TransportInterProcess20160622H
#define TransportInterProcess20160622H

#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <thread>
#include <atomic>

#include "goby/common/zeromq_service.h"

#include "transport-common.h"
#include "goby/sandbox/protobuf/zmq_transporter_config.pb.h"

namespace goby
{   

    enum { ZMQ_MARSHALLING_SCHEME = 0x474f4259 };
    
    template<typename InnerTransporter>
        class InterProcessTransporter : public SerializationTransporterBase<InnerTransporter>
        {
        public:
        InterProcessTransporter(InnerTransporter& inner) : SerializationTransporterBase<InnerTransporter>(inner) { }
            ~InterProcessTransporter() { }

            static const std::string forward_group;
        private:
            const std::string& forward_group_name() { return forward_group; }
        };

    template<typename InnerTransport>
        const std::string InterProcessTransporter<InnerTransport>::forward_group( "goby::InterProcessTransporter");
    
    
    
    template<typename InnerTransporter = NoOpTransporter>
        class ZMQTransporter
        {
        public:
        ZMQTransporter(const protobuf::ZMQTransporterConfig& cfg) : own_inner_(new InnerTransporter), inner_(*own_inner_), cfg_(cfg)
        { _init(); }

        ZMQTransporter(InnerTransporter& inner, const protobuf::ZMQTransporterConfig& cfg) : inner_(inner), cfg_(cfg)
        { _init(); }
        
        ~ZMQTransporter() { }

        // direct publications (possibly without an "InnerTransporter")
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

        // direct subscriptions (possibly without an "InnerTransporter")
        template<typename DataType, int scheme = scheme<DataType>()>
        void subscribe(const std::string& group, std::function<void(const DataType&)> func)
        {
            inner_.subscribe<DataType, scheme>(group, func);
            std::string identifier = _make_identifier<DataType, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

            auto subscribe_lambda = [&](std::shared_ptr<DataType> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { func(*d); };
            typename SerializationSubscription<DataType, scheme>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(new SerializationSubscription<DataType, scheme>(subscribe_function, group));
            
            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(ZMQ_MARSHALLING_SCHEME, identifier, SOCKET_SUBSCRIBE);
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
        void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
        {
            subscribe<DataType, scheme>(group, [=](const DataType& d) { (c->*mem_func)(d); });
        }

        int poll(std::chrono::system_clock::duration wait_for)
        {
            int items = inner_.poll(std::chrono::seconds(0));
            return items + zmq_.poll(std::chrono::duration_cast<std::chrono::microseconds>(wait_for).count());
        }
    
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return poll(timeout - std::chrono::system_clock::now());
        }
        
        private:
        void _init()
        {
            inner_.subscribe<goby::protobuf::SerializerTransporterData>(InterProcessTransporter<InnerTransporter>::forward_group, &ZMQTransporter::_receive_publication_forwarded, this);
            inner_.subscribe<SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>(InterProcessTransporter<InnerTransporter>::forward_group, &ZMQTransporter::_receive_subscription_forwarded, this);

            zmq_.connect_inbox_slot(&ZMQTransporter::_zmq_inbox, this);

            goby::common::protobuf::ZeroMQServiceConfig cfg;

            goby::common::protobuf::ZeroMQServiceConfig::Socket* query_socket = cfg.add_socket();
            query_socket->set_socket_type(common::protobuf::ZeroMQServiceConfig::Socket::REQUEST);
            query_socket->set_socket_id(SOCKET_MANAGER);

            switch(cfg_.transport())
            {
                case protobuf::ZMQTransporterConfig::IPC:
                    query_socket->set_transport(common::protobuf::ZeroMQServiceConfig::Socket::IPC);
                    query_socket->set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".manager");
                    break;
                case protobuf::ZMQTransporterConfig::TCP:
                    query_socket->set_transport(common::protobuf::ZeroMQServiceConfig::Socket::TCP);
                    query_socket->set_ethernet_address(cfg_.ipv4_address());
                    query_socket->set_ethernet_port(cfg_.tcp_port());
                    break;
            }
            query_socket->set_connect_or_bind(common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);

            zmq_.set_cfg(&cfg);
            
            protobuf::ZMQManagerRequest req;
            req.set_request(protobuf::PROVIDE_PUB_SUB_SOCKETS);
            zmq_.send(ZMQ_MARSHALLING_SCHEME, "", req.SerializeAsString(), SOCKET_MANAGER);
            poll(std::chrono::seconds(cfg_.manager_timeout_seconds()));
            
            if(!have_pubsub_sockets_)
            {
                std::cerr << "No response from manager: " << cfg_.ShortDebugString() << std::endl;
                exit(EXIT_FAILURE);
            }
            
        }
        
        void _receive_publication_forwarded(std::shared_ptr<const goby::protobuf::SerializerTransporterData> data)
        {
            zmq_.send(ZMQ_MARSHALLING_SCHEME,
                      _make_identifier(data->type(), data->marshalling_scheme(), data->group(), IdentifierWildcard::NO_WILDCARDS),
                      data->data(),
                      SOCKET_PUBLISH);

        }

        void _receive_subscription_forwarded(std::shared_ptr<const SerializationSubscriptionBase> subscription)
        {
            std::cout << "Subscription forwarded to us: " << subscription->type_name() << ":" << subscription->group() << std::endl;
            std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(), subscription->group(), IdentifierWildcard::PROCESS_THREAD_WILDCARD);

            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(ZMQ_MARSHALLING_SCHEME, identifier, SOCKET_SUBSCRIBE);
        }

        
        template<typename DataType, int scheme>
        void _publish(const DataType& d, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<DataType, scheme>::serialize(d));
            std::string sbytes(bytes.begin(), bytes.end());
            zmq_.send(ZMQ_MARSHALLING_SCHEME, _make_identifier<DataType, scheme>(group, IdentifierWildcard::NO_WILDCARDS), sbytes, SOCKET_PUBLISH);
        }

        void _zmq_inbox(int marshalling_scheme,
                        const std::string& identifier,
                        const std::string& body,
                        int socket_id)
        {
            if(marshalling_scheme != ZMQ_MARSHALLING_SCHEME)
                return;
            
            if(socket_id == SOCKET_SUBSCRIBE)
            {
                for(auto &sub : subscriptions_)
                {
                    if(identifier.compare(0, sub.first.size(), sub.first) == 0)
                    {
                        sub.second->post(body.begin(), body.end());
                    }
                }
            }
            else if(socket_id == SOCKET_MANAGER)
            {
                protobuf::ZMQManagerResponse response;
                response.ParseFromString(body);
                if(response.request() == protobuf::PROVIDE_PUB_SUB_SOCKETS)
                {
                    goby::common::protobuf::ZeroMQServiceConfig cfg;

                    response.mutable_subscribe_socket()->set_socket_id(SOCKET_SUBSCRIBE);
                    response.mutable_publish_socket()->set_socket_id(SOCKET_PUBLISH);

                    if(response.subscribe_socket().transport() == common::protobuf::ZeroMQServiceConfig::Socket::TCP)
                        response.mutable_subscribe_socket()->set_ethernet_address(cfg_.ipv4_address());
                    if(response.publish_socket().transport() == common::protobuf::ZeroMQServiceConfig::Socket::TCP)
                        response.mutable_publish_socket()->set_ethernet_address(cfg_.ipv4_address());
                    
                    *cfg.add_socket() = response.publish_socket();
                    *cfg.add_socket() = response.subscribe_socket();
                    zmq_.merge_cfg(&cfg);
                    have_pubsub_sockets_ = true;
                }
            }
        }

        enum class IdentifierWildcard { NO_WILDCARDS,
                                        THREAD_WILDCARD,
                                        PROCESS_THREAD_WILDCARD };
        
        template<typename DataType, int scheme>
        std::string _make_identifier(const std::string& group, IdentifierWildcard wildcard)
        {
            return _make_identifier(SerializerParserHelper<DataType, scheme>::type_name(DataType()), scheme, group, wildcard);
        }

        std::string _make_identifier(const std::string& type_name, int scheme, const std::string& group, IdentifierWildcard wildcard)
        {
            std::string sscheme(std::to_string(scheme));
            std::string process(std::to_string(getpid()));
            std::string thread(std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
            
            std::string id("/" + group +
                           "/" + sscheme +
                           "/" + type_name + 
                           "/");
            if(wildcard != IdentifierWildcard::PROCESS_THREAD_WILDCARD)
                id += process + "/";
            if(wildcard == IdentifierWildcard::NO_WILDCARDS)
                id += thread + "/";
            return id;
        }
        
        private:

        enum { SOCKET_MANAGER = 0, SOCKET_SUBSCRIBE = 1, SOCKET_PUBLISH = 2 };
        
        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        const protobuf::ZMQTransporterConfig& cfg_;
        goby::common::ZeroMQService zmq_;
        bool have_pubsub_sockets_{false};
        
        // maps identifier to subscription
        std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>> subscriptions_;
    };
    
    class ZMQRouter
    {
    public:
    ZMQRouter(zmq::context_t& context, const goby::protobuf::ZMQTransporterConfig& cfg) :
        context_(context),
            cfg_(cfg)
            { }

        void run();
        unsigned last_port(zmq::socket_t& socket);

        
        ZMQRouter(ZMQRouter&) = delete;
        ZMQRouter& operator=(ZMQRouter&) = delete;
    
    public:
        std::atomic<unsigned> pub_port{0};
        std::atomic<unsigned> sub_port{0};
    
    private:
        zmq::context_t& context_;
        const goby::protobuf::ZMQTransporterConfig& cfg_;
    
    };

    class ZMQManager
    {
    public:
    ZMQManager(zmq::context_t& context, const goby::protobuf::ZMQTransporterConfig& cfg, const ZMQRouter& router) :
        context_(context),
            cfg_(cfg),
            router_(router)
            { }

        void run();
        
    private:
        zmq::context_t& context_;
        const goby::protobuf::ZMQTransporterConfig& cfg_;
        const ZMQRouter& router_;
    };


}


#endif
