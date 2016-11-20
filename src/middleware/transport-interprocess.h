#ifndef TransportInterProcess20160622H
#define TransportInterProcess20160622H

#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <thread>
#include <atomic>
#include <tuple>

#include "zeromq_service.h"

#include "transport-common.h"
#include "goby/middleware/protobuf/interprocess_config.pb.h"


namespace goby
{   
    template<typename Derived, typename InnerTransporter>
        class InterProcessTransporterBase
    {
    public:        
    InterProcessTransporterBase(InnerTransporter& inner) : inner_(inner) { }
    InterProcessTransporterBase() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
            
        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void publish(const Data& data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Derived*>(this)->template _publish<group, Data, scheme>(data, transport_cfg);
                inner_.template publish<group, Data, scheme>(data, transport_cfg);
            }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void publish(std::shared_ptr<Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                if(data)
                {
                    check_validity<group>();
                    static_cast<Derived*>(this)->template _publish<group, Data, scheme>(*data, transport_cfg);
                    inner_.template publish<group, Data, scheme>(data, transport_cfg);
                }
            }
        
        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(const Data&)> func)
            {
                check_validity<group>();
                inner_.template subscribe<group, Data, scheme>(func);
                static_cast<Derived*>(this)->template _subscribe<group, Data, scheme>([=](std::shared_ptr<const Data> d) { func(*d); });
            }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> func)
            {
                check_validity<group>();
                inner_.template subscribe<group, Data, scheme>(func);
                static_cast<Derived*>(this)->template _subscribe<group, Data, scheme>(func);
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
        static constexpr Group forward_group_ { "goby::InterProcessForwarder" };

    private:
        template<const Group& group>
            void check_validity()
        {
            static_assert((group.c_str()[0] != '\0') || (int(group) != 0), "goby::Group must have non-zero length string or non-zero integer value.");
        }
        
    };    
    
    template<typename Derived, typename InnerTransporter>
        constexpr goby::Group InterProcessTransporterBase<Derived, InnerTransporter>::forward_group_;
    
    template<typename InnerTransporter>
        class InterProcessForwarder : public InterProcessTransporterBase<InterProcessForwarder<InnerTransporter>, InnerTransporter>
    {
    public:
        using Base = InterProcessTransporterBase<InterProcessForwarder<InnerTransporter>, InnerTransporter>;

        InterProcessForwarder(InnerTransporter& inner) : Base(inner)
        { }

        friend Base;
    private:
        template<const Group& group, typename Data, int scheme>
            void _publish(const Data& d, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            // create and forward publication to edge
            std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
            std::string* sbytes = new std::string(bytes.begin(), bytes.end());
            std::shared_ptr<goby::protobuf::SerializerTransporterData> data = std::make_shared<goby::protobuf::SerializerTransporterData>();

            data->set_marshalling_scheme(scheme);
            data->set_type(SerializerParserHelper<Data, scheme>::type_name(d));
            data->set_group(std::string(group));
            data->set_allocated_data(sbytes);
        
            *data->mutable_cfg() = transport_cfg;

            Base::inner_.template publish<Base::forward_group_>(data);
        }
        
        template<const Group& group, typename Data, int scheme>
            void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func)
        {
            // forward subscription to edge
            auto inner_publication_lambda = [&](std::shared_ptr<Data> d, const goby::protobuf::TransporterConfig& t) { Base::inner_.template publish<group, Data, scheme>(d, t); };
            typename SerializationSubscription<Data, scheme>::HandlerType inner_publication_function(inner_publication_lambda);

            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, scheme>(inner_publication_function,
                                                            group,
                                                            [=](const Data&d) { return group; }));
                    
            Base::inner_.template publish<Base::forward_group_, SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>(subscription);
        }
        
        int _poll(std::chrono::system_clock::duration wait_for)
        {
            return Base::inner_.poll(wait_for);
        }
    };    

    
    
    template<typename InnerTransporter = NoOpTransporter>
        class InterProcessPortal : public InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter>
        {
        public:
        using Base = InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter>;

        InterProcessPortal(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
        { _init(); }

        InterProcessPortal(InnerTransporter& inner, const protobuf::InterProcessPortalConfig& cfg) : Base(inner), cfg_(cfg)
        { _init(); }

        friend Base;
        private:
        
        template<const Group& group, typename Data, int scheme>
        void _publish(const Data& d, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
            std::string identifier = _make_fully_qualified_identifier<group, Data, scheme>();
            
            zmq::message_t msg(identifier.size() + 1 + bytes.size());
            memcpy(msg.data(), identifier.data(), identifier.size());
            *(static_cast<char*>(msg.data())+identifier.size()) = '\0';
            memcpy(static_cast<char*>(msg.data())+identifier.size() + 1,
                   &bytes[0], bytes.size());            

            zmq_.send(msg, SOCKET_PUBLISH);
        }
        

        template<const Group& group, typename Data, int scheme>
        void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func)
        {
            std::string identifier = _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);
            auto subscribe_lambda = [=](std::shared_ptr<Data> d, const goby::protobuf::TransporterConfig& t) { func(d); };
            typename SerializationSubscription<Data, scheme>::HandlerType subscribe_function(subscribe_lambda);

            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, scheme>(subscribe_function,
                                                            group,
                                                            [=](const Data&d) { return group; })); 
            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(identifier, SOCKET_SUBSCRIBE);
        }
        
        int _poll(std::chrono::system_clock::duration wait_for)
        {
            int items = Base::inner_.poll(std::chrono::seconds(0));
            return items + zmq_.poll(std::chrono::duration_cast<std::chrono::microseconds>(wait_for).count());
        }
        
        void _init()
        {
            using goby::protobuf::SerializerTransporterData;
            Base::inner_.template subscribe<Base::forward_group_, SerializerTransporterData>([this](std::shared_ptr<const SerializerTransporterData> d) { _receive_publication_forwarded(d);});
            
            Base::inner_.template subscribe<Base::forward_group_, SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>([this](std::shared_ptr<const SerializationSubscriptionBase> s) { _receive_subscription_forwarded(s); });
            zmq_.receive_func = [&](const void* data, int size, int message_part, int socket_id) { _zmq_inbox(data, size, message_part, socket_id); };

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

            zmq::message_t msg(1 + req.ByteSize());
            *static_cast<char*>(msg.data()) = '\0';
            req.SerializeToArray(static_cast<char*>(msg.data())+1, req.ByteSize());
            zmq_.send(msg, SOCKET_MANAGER);
            Base::poll(std::chrono::seconds(cfg_.manager_timeout_seconds()));
            
            if(!have_pubsub_sockets_)
            {
                std::cerr << "No response from manager: " << cfg_.ShortDebugString() << std::endl;
                exit(EXIT_FAILURE);
            }
            
        }
        
        void _receive_publication_forwarded(std::shared_ptr<const goby::protobuf::SerializerTransporterData> data)
        {
            std::string identifier = _make_identifier(data->type(), data->marshalling_scheme(), data->group(), IdentifierWildcard::NO_WILDCARDS) + '\0';
            const auto& bytes = data->data();
            zmq::message_t msg(identifier.size() + bytes.size());
            memcpy(msg.data(), identifier.data(), identifier.size());
            memcpy(static_cast<char*>(msg.data())+identifier.size(),
                   bytes.data(), bytes.size());
                        
            zmq_.send(msg, SOCKET_PUBLISH);
        }

        void _receive_subscription_forwarded(std::shared_ptr<const SerializationSubscriptionBase> subscription)
        {
            std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(), subscription->subscribed_group(), IdentifierWildcard::PROCESS_THREAD_WILDCARD);

            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(identifier, SOCKET_SUBSCRIBE);
        }

        void _zmq_inbox(const void* data, int size, int message_part, int socket_id)
        {
            int null_delim_pos = 0;
            for(int i = 0; i < size; ++i)
            {
                if(*(static_cast<const char*>(data) + i) == '\0')
                {
                    null_delim_pos = i;
                    break;
                }
            }
            if(socket_id == SOCKET_SUBSCRIBE)
            {
                for(auto &sub : subscriptions_)
                {
                    if(static_cast<unsigned>(size) >= sub.first.size() && memcmp(data, sub.first.data(), sub.first.size()) == 0)
                        sub.second->post(static_cast<const char*>(data) + null_delim_pos + 1,
                                         static_cast<const char*>(data) + size);
                }
            }
            else if(socket_id == SOCKET_MANAGER)
            {
                protobuf::ZMQManagerResponse response;
                response.ParseFromArray(static_cast<const char*>(data) + null_delim_pos + 1, size - (null_delim_pos + 1));
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


        template<const Group& group, typename Data, int scheme>
        std::string _make_fully_qualified_identifier()
        {
            // make all but the thread part once and reuse
            static const std::string id(_make_identifier<Data, scheme>(group, IdentifierWildcard::THREAD_WILDCARD));
            return id + id_component(std::this_thread::get_id(), threads_);
        }
        
        
        template<typename Data, int scheme>
        std::string _make_identifier(const Group& group, IdentifierWildcard wildcard)
        {
            return _make_identifier(SerializerParserHelper<Data, scheme>::type_name(Data()), scheme, group, wildcard);
        }

        std::string _make_identifier(const std::string& type_name, int scheme, const std::string& group, IdentifierWildcard wildcard)
        {
            switch(wildcard)
            {
                default:
                case IdentifierWildcard::NO_WILDCARDS:
                return ("/" +
                        group + "/" +
                        id_component(scheme, schemes_) +
                        type_name + "/" +
                        process_ + "/" +
                        id_component(std::this_thread::get_id(), threads_));
                case IdentifierWildcard::THREAD_WILDCARD:
                return ("/" +
                        group +  "/" +
                        id_component(scheme, schemes_) +
                        type_name + "/" +
                        process_ + "/");
                case IdentifierWildcard::PROCESS_THREAD_WILDCARD:
                   return ("/" +
                           group + "/" +
                           id_component(scheme, schemes_) +
                           type_name + "/");
            }
        }        

        template<typename Key>
        const std::string& id_component(const Key& k, std::unordered_map<Key, std::string>& map)
        {
            auto it = map.find(k);
            if(it != map.end()) return it->second;

            std::string v = to_string(k) + "/";
            auto it_pair = map.insert(std::make_pair(k, v));
            return it_pair.first->second;
        }

        std::string to_string(int i) { return std::to_string(i); }
        std::string to_string(std::thread::id i) { return std::to_string(std::hash<std::thread::id>{}(i)); }
        
        
        private:

        enum { SOCKET_MANAGER = 0, SOCKET_SUBSCRIBE = 1, SOCKET_PUBLISH = 2 };
        
        const protobuf::InterProcessPortalConfig& cfg_;
        goby::middleware::ZeroMQService zmq_;
        bool have_pubsub_sockets_{false};
        
        // maps identifier to subscription
        std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>> subscriptions_;
        std::string process_ {std::to_string(getpid())};
        std::unordered_map<int, std::string> schemes_;
        std::unordered_map<std::thread::id, std::string> threads_;
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
