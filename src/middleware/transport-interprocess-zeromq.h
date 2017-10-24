#ifndef TransportInterProcessZeroMQ20170807H
#define TransportInterProcessZeroMQ20170807H

#include "zeromq_service.h"

#include "transport-interprocess.h"


namespace goby
{
        template<typename InnerTransporter = NullTransporter>
        class InterProcessPortal : public InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter>
        {
        public:
        using Base = InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter>;

        InterProcessPortal(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
        { _init(); }

        InterProcessPortal(InnerTransporter& inner, const protobuf::InterProcessPortalConfig& cfg) : Base(inner), cfg_(cfg)
        { _init(); }

        ~InterProcessPortal()
        {
            zmq_alive_ = false;
            if(zmq_thread_)
                zmq_thread_->join();
        }
        
        
        
        friend Base;
        private:
        
        template<typename Data, int scheme>
        void _publish(const Data& d, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
            std::string identifier = _make_fully_qualified_identifier<Data, scheme>(group);
            
            zmq::message_t msg(identifier.size() + 1 + bytes.size());
            memcpy(msg.data(), identifier.data(), identifier.size());
            *(static_cast<char*>(msg.data())+identifier.size()) = '\0';
            memcpy(static_cast<char*>(msg.data())+identifier.size() + 1,
                   &bytes[0], bytes.size());            

            zmq_.send(msg, SOCKET_PUBLISH);
        }
        

        template<typename Data, int scheme>
        void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f, const Group& group)
        {
            std::string identifier = _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);
            auto subscribe_lambda = [=](std::shared_ptr<const Data> d, const goby::protobuf::TransporterConfig& t) { f(d); };
            typename SerializationSubscription<Data, scheme>::HandlerType subscribe_function(subscribe_lambda);

            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, scheme>(subscribe_function,
                                                            group,
                                                            [=](const Data&d) { return group; })); 
            subscriptions_.insert(std::make_pair(identifier, subscription));
            zmq_.subscribe(identifier, SOCKET_SUBSCRIBE);
        }
        
        int _poll()
        {
            int items = 0;
            if(have_pubsub_sockets_)
            {
                auto& socket = zmq_.socket_from_id(SOCKET_SUBSCRIBE);
                std::lock_guard<std::mutex> lock(socket.mutex());
                auto& buffer = socket.buffer();
                items += buffer.size();
                for(const auto& msg_buffer : buffer)
                    _process_zmq_message(msg_buffer, SOCKET_SUBSCRIBE);
                buffer.clear();
            }
            else
            {
                auto& socket = zmq_.socket_from_id(SOCKET_MANAGER);
                socket.mutex().lock();
                // make copy so we can do merge_cfg();
                auto buffer = socket.buffer();
                socket.buffer().clear();
                socket.mutex().unlock();
                items += buffer.size();
                for(const auto& msg_buffer : buffer)
                    _process_zmq_message(msg_buffer, SOCKET_MANAGER);
            }

            goby::glog.is(goby::common::logger::DEBUG1) && goby::glog <<  "Transport Interprocess ZeroMQ::_poll: " << items << std::endl;
            
            return items;
        }
        
        void _init()
        {
            using goby::protobuf::SerializerTransporterData;
            Base::inner_.template subscribe<Base::forward_group_, SerializerTransporterData>([this](std::shared_ptr<const SerializerTransporterData> d) { _receive_publication_forwarded(d);});
            
            Base::inner_.template subscribe<Base::forward_group_, SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>([this](std::shared_ptr<const SerializationSubscriptionBase> s) { _receive_subscription_forwarded(s); });

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

            zmq_.merge_cfg(&cfg);
            
            protobuf::ZMQManagerRequest req;
            req.set_request(protobuf::PROVIDE_PUB_SUB_SOCKETS);

            zmq::message_t msg(1 + req.ByteSize());
            *static_cast<char*>(msg.data()) = '\0';
            req.SerializeToArray(static_cast<char*>(msg.data())+1, req.ByteSize());
            zmq_.send(msg, SOCKET_MANAGER);


            // start zmq poll thread
            zmq_thread_.reset(new std::thread(
                                  [this](){
                                      const int zmq_ms_timeout = 100;
                                      while (zmq_alive_) {
                                          if(zmq_.poll(zmq_ms_timeout))
                                              PollerInterface::cv()->notify_all();
                                      } }));
            
            auto start = std::chrono::system_clock::now();
            while(!have_pubsub_sockets_ && (start + std::chrono::seconds(cfg_.manager_timeout_seconds()) > std::chrono::system_clock::now()))
                Base::poll(start + std::chrono::seconds(cfg_.manager_timeout_seconds()));
            
            if(!have_pubsub_sockets_)
                goby::glog.is(goby::common::logger::DIE) && goby::glog << "No response from gobyd: " << cfg_.ShortDebugString() << std::endl;

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

        void _process_zmq_message(const std::vector<char>& data, int socket_id)
        {
            size_t size = data.size();
            
            int null_delim_pos = 0;
            for(auto i = std::vector<char>::size_type(0), n = data.size(); i < n; ++i)
            {
                if(data[i] == '\0')
                {
                    null_delim_pos = i;
                    break;
                }
            }
            if(socket_id == SOCKET_SUBSCRIBE)
            {                
                for(auto &sub : subscriptions_)
                {                    
                    if(static_cast<unsigned>(size) >= sub.first.size() && memcmp(&data[0], sub.first.data(), sub.first.size()) == 0)
                        sub.second->post(data.begin()+null_delim_pos+1, data.end());
                }
            }
            else if(socket_id == SOCKET_MANAGER)
            {
                protobuf::ZMQManagerResponse response;
                response.ParseFromArray(&data[0] + null_delim_pos + 1, size - (null_delim_pos + 1));
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
        std::string _make_fully_qualified_identifier(const Group& group)
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

        // runs zmq_.poll()
        std::unique_ptr<std::thread> zmq_thread_;
        std::atomic<bool> zmq_alive_{true};
        
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

