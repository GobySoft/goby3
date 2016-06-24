#ifndef TransportInterProcess20160622H
#define TransportInterProcess20160622H

#include <sys/types.h>
#include <unistd.h>
#include <functional>

#include "goby/common/zeromq_service.h"

#include "transport-common.h"
#include "goby/sandbox/protobuf/interprocess_data.pb.h"

namespace goby
{   

    enum { ZMQ_MARSHALLING_SCHEME = 0x474f4259 };

    
    class InterProcessSubscriptionBase
    {
    public:
        virtual void post(const std::vector<char>& body) const = 0;
        virtual const std::string& type_name() const = 0;
        virtual const std::string& group() const = 0;
        virtual int scheme() const = 0;
    };

    template<typename DataType, int scheme_id>
        class InterProcessSubscription : public InterProcessSubscriptionBase
    {
    public:
        typedef std::function<void (std::shared_ptr<DataType> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)> HandlerType;

    InterProcessSubscription(HandlerType& handler,
                             const std::string& group)
        : handler_(handler),
            type_name_(SerializerParserHelper<DataType, scheme_id>::type_name(DataType())),
            group_(group)
            { }
            
        // handle an incoming message
        void post(const std::vector<char>& body) const
        {
            auto msg = std::make_shared<DataType>(SerializerParserHelper<DataType, scheme_id>::parse(body));
            handler_(msg, group_, goby::protobuf::TransporterConfig());
        }

        // getters
        const std::string& type_name() const { return type_name_; }
        const std::string& group() const { return group_; }            
        int scheme() const { return scheme_id; }
            
    private:
        HandlerType handler_;
        const std::string type_name_;
        const std::string group_;
    };
    
    
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

        
        
        template<typename DataType, int scheme = scheme<DataType>()>
        void subscribe(const std::string& group, std::function<void(const DataType&)> func)
        {
            // subscribe to inner transporter
            inner_.subscribe<DataType, scheme>(group, func);

            // forward subscription to edge
            auto inner_publication_lambda = [&](std::shared_ptr<DataType> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { inner_.template publish<DataType, scheme>(d, g, t); };
            typename InterProcessSubscription<DataType, scheme>::HandlerType inner_publication_function(inner_publication_lambda);
            auto subscription = std::shared_ptr<InterProcessSubscriptionBase>(new InterProcessSubscription<DataType, scheme>(inner_publication_function, group));
            inner_.publish<InterProcessSubscriptionBase, MarshallingScheme::CXX_OBJECT>(subscription, forward_group);
            std::cout << "Published subscription: " << subscription->type_name() << ":" << subscription->group() << std::endl;
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
        void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
        {
            subscribe<DataType, scheme>(group, [=](const DataType& d) { (c->*mem_func)(d); });
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
            // create and forward publication to edge
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

        // direct subscriptions (possibly without an "InnerTransporter")
        template<typename DataType, int scheme = scheme<DataType>()>
        void subscribe(const std::string& group, std::function<void(const DataType&)> func)
        {
            inner_.subscribe<DataType, scheme>(group, func);
            std::string identifier = _make_identifier<DataType, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);
            //local_subscriptions_.insert(std::make_pair(identifier, f));
            zmq_.subscribe(ZMQ_MARSHALLING_SCHEME, identifier, SOCKET_SUBSCRIBE);
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
        void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
        {
            subscribe<DataType, scheme>(group, [=](const DataType& d) { (c->*mem_func)(d); });
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
            inner_.subscribe<goby::protobuf::InterProcessData>(InterProcessTransporter<InnerTransporter>::forward_group, &ZMQTransporter::_receive_publication_forwarded, this);
            inner_.subscribe<InterProcessSubscriptionBase, MarshallingScheme::CXX_OBJECT>(InterProcessTransporter<InnerTransporter>::forward_group, &ZMQTransporter::_receive_subscription_forwarded, this);

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
        
        void _receive_publication_forwarded(std::shared_ptr<const goby::protobuf::InterProcessData> data)
        {
            zmq_.send(ZMQ_MARSHALLING_SCHEME,
                      _make_identifier(data->type(), data->marshalling_scheme(), data->group(), IdentifierWildcard::NO_WILDCARDS),
                      data->data(),
                      SOCKET_PUBLISH);

        }

        void _receive_subscription_forwarded(std::shared_ptr<const InterProcessSubscriptionBase> subscription)
        {
            std::cout << "Subscription forwarded to us: " << subscription->type_name() << ":" << subscription->group() << std::endl;
            std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(), subscription->group(), IdentifierWildcard::PROCESS_THREAD_WILDCARD);

            forwarded_subscriptions_[identifier] = subscription;
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
            for(auto &sub : forwarded_subscriptions_)
            {
                std::cout << "Ide: [" << goby::util::hex_encode(identifier) << "]" << std::endl;

                std::cout << "Sub: [" << goby::util::hex_encode(sub.first) << "]" << std::endl;
                if(identifier.compare(0, sub.first.size(), sub.first) == 0)
                {
                    std::vector<char> data(body.begin(), body.end());
                    sub.second->post(data);
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
        goby::common::ZeroMQService zmq_;
        int publish_port_ = { 0 };

        // maps identifier to forwarded subscription
        std::unordered_map<std::string, std::shared_ptr<const InterProcessSubscriptionBase>> forwarded_subscriptions_;

        // maps identifier to local subscription function
        // std::unordered_multimap<std::string, std::function<void(const DataType&)>> local_subscriptions_;
        };
}


#endif
