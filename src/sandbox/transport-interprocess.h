#ifndef TransportInterProcess20160622H
#define TransportInterProcess20160622H

#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <thread>
#include <atomic>

#include "goby/common/zeromq_service.h"

#include "transport-common.h"
#include "goby/sandbox/protobuf/interprocess_config.pb.h"

namespace goby
{   

    enum { ZMQ_MARSHALLING_SCHEME = 0x474f4259 };



    template<typename Derived, typename InnerTransporter, typename Group>
        class InterProcessTransporterBase
    {
    public:        
    InterProcessTransporterBase(InnerTransporter& inner) : inner_(inner) { }
    InterProcessTransporterBase() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
            
        template<typename Data, int scheme = scheme<Data>()>
            void publish(const Data& data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                static_cast<Derived*>(this)->template _publish<Data, scheme>(data, group, transport_cfg);
                inner_.publish<Data, scheme>(data, group_convert(group), transport_cfg);
            }

        template<typename Data, int scheme = scheme<Data>()>
            void publish(std::shared_ptr<Data> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                if(data)
                {
                    static_cast<Derived*>(this)->template _publish<Data, scheme>(*data, group, transport_cfg);
                    inner_.publish<Data, scheme>(data, group_convert(group), transport_cfg);
                }
            }
        
        template<typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(const Data&)> func, const Group& group = Group())
            {
                inner_.subscribe<Data, scheme>(func, group_convert(group));
                static_cast<Derived*>(this)->template _subscribe<Data, scheme>([=](std::shared_ptr<const Data> d) { func(*d); }, group);
            }

        template<typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> func, const Group& group = Group())
            {
                inner_.subscribe<Data, scheme>(func, group_convert(group));
                static_cast<Derived*>(this)->template _subscribe<Data, scheme>(func, group);
            }
        
        
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return poll(timeout - std::chrono::system_clock::now());
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            return static_cast<Derived*>(this)->_poll(wait_for);
        }

        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        const std::string forward_group_ { "goby::InterProcessForwarder" };
    };    
    
    template<typename InnerTransporter>
        class InterProcessForwarder : public InterProcessTransporterBase<InterProcessForwarder<InnerTransporter>, InnerTransporter, std::string>
    {
    public:
        using Group = std::string;
        using Base = InterProcessTransporterBase<InterProcessForwarder<InnerTransporter>, InnerTransporter, Group>;

        InterProcessForwarder(InnerTransporter& inner) : Base(inner)
        { }

        friend Base;
    private:
        template<typename Data, int scheme>
            void _publish(const Data& d, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            // create and forward publication to edge
            std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
            std::string* sbytes = new std::string(bytes.begin(), bytes.end());
            std::shared_ptr<goby::protobuf::SerializerTransporterData> data = std::make_shared<goby::protobuf::SerializerTransporterData>();

            data->set_marshalling_scheme(scheme);
            data->set_type(SerializerParserHelper<Data, scheme>::type_name(d));
            data->set_group(group_convert(group));
            data->set_allocated_data(sbytes);
        
            *data->mutable_cfg() = transport_cfg;

            Base::inner_.publish(data, Base::forward_group_);
        }
        
        template<typename Data, int scheme>
            void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group)
        {
            // forward subscription to edge
            auto inner_publication_lambda = [&](std::shared_ptr<Data> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { Base::inner_.template publish<Data, scheme>(d, g, t); };
            typename SerializationSubscription<Data, scheme>::HandlerType inner_publication_function(inner_publication_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, scheme>(inner_publication_function,
                                                            group_convert(group),
                                                            [=](const Data&d) { return group; }));
                    
            Base::inner_.template publish<SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>(subscription, Base::forward_group_);
        }
        
        int _poll(std::chrono::system_clock::duration wait_for)
        {
            return Base::inner_.poll(wait_for);
        }
    };    

    
    
    template<typename InnerTransporter = NoOpTransporter>
        class InterProcessPortal : public InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter, std::string>
        {
        public:
        using Group = std::string;
        using Base = InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter, Group>;

        InterProcessPortal(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
        { _init(); }

        InterProcessPortal(InnerTransporter& inner, const protobuf::InterProcessPortalConfig& cfg) : Base(inner), cfg_(cfg)
        { _init(); }

        friend Base;
        private:
        
        template<typename Data, int scheme>
        void _publish(const Data& d, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
            std::string sbytes(bytes.begin(), bytes.end());
            zmq_.send(ZMQ_MARSHALLING_SCHEME, _make_identifier<Data, scheme>(group, IdentifierWildcard::NO_WILDCARDS), sbytes, SOCKET_PUBLISH);
        }

        template<typename Data, int scheme>
        void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group)
        {
            std::string identifier = _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);
            auto subscribe_lambda = [=](std::shared_ptr<Data> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { func(d); };
            typename SerializationSubscription<Data, scheme>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, scheme>(subscribe_function,
                                                                group_convert(group),
                                                                [=](const Data&d) { return group; })); 
            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(ZMQ_MARSHALLING_SCHEME, identifier, SOCKET_SUBSCRIBE);
        }
        
        int _poll(std::chrono::system_clock::duration wait_for)
        {
            int items = Base::inner_.poll(std::chrono::seconds(0));
            return items + zmq_.poll(std::chrono::duration_cast<std::chrono::microseconds>(wait_for).count());
        }
        
        void _init()
        {
            using goby::protobuf::SerializerTransporterData;
            Base::inner_.template subscribe<SerializerTransporterData>([this](std::shared_ptr<const SerializerTransporterData> d) { _receive_publication_forwarded(d);}, Base::forward_group_);
            
            Base::inner_.template subscribe<SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>([this](std::shared_ptr<const SerializationSubscriptionBase> s) { _receive_subscription_forwarded(s); }, Base::forward_group_);

            zmq_.connect_inbox_slot(&InterProcessPortal::_zmq_inbox, this);

            goby::common::protobuf::ZeroMQServiceConfig cfg;

            goby::common::protobuf::ZeroMQServiceConfig::Socket* query_socket = cfg.add_socket();
            query_socket->set_socket_type(common::protobuf::ZeroMQServiceConfig::Socket::REQUEST);
            query_socket->set_socket_id(SOCKET_MANAGER);

            switch(cfg_.transport())
            {
                case protobuf::InterProcessPortalConfig::IPC:
                    query_socket->set_transport(common::protobuf::ZeroMQServiceConfig::Socket::IPC);
                    query_socket->set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".manager");
                    break;
                case protobuf::InterProcessPortalConfig::TCP:
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
            Base::poll(std::chrono::seconds(cfg_.manager_timeout_seconds()));
            
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
            std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(), subscription->subscribed_group(), IdentifierWildcard::PROCESS_THREAD_WILDCARD);

            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(ZMQ_MARSHALLING_SCHEME, identifier, SOCKET_SUBSCRIBE);
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
        
        template<typename Data, int scheme>
        std::string _make_identifier(const Group& group, IdentifierWildcard wildcard)
        {
            return _make_identifier(SerializerParserHelper<Data, scheme>::type_name(Data()), scheme, group, wildcard);
        }

        std::string _make_identifier(const std::string& type_name, int scheme, const Group& group, IdentifierWildcard wildcard)
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
        
        const protobuf::InterProcessPortalConfig& cfg_;
        goby::common::ZeroMQService zmq_;
        bool have_pubsub_sockets_{false};
        
        // maps identifier to subscription
        std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>> subscriptions_;
    };
    
    class ZMQRouter
    {
    public:
    ZMQRouter(zmq::context_t& context, const goby::protobuf::InterProcessPortalConfig& cfg) :
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
        const goby::protobuf::InterProcessPortalConfig& cfg_;
    
    };

    class ZMQManager
    {
    public:
    ZMQManager(zmq::context_t& context, const goby::protobuf::InterProcessPortalConfig& cfg, const ZMQRouter& router) :
        context_(context),
            cfg_(cfg),
            router_(router)
            { }

        void run();
        
    private:
        zmq::context_t& context_;
        const goby::protobuf::InterProcessPortalConfig& cfg_;
        const ZMQRouter& router_;
    };


}


#endif
