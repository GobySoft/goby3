#ifndef TransportInterProcessZeroMQ20170807H
#define TransportInterProcessZeroMQ20170807H

#include <zmq.hpp>

#include "transport-interprocess.h"
#include "goby/middleware/protobuf/interprocess_zeromq.pb.h"
#include "goby/common/protobuf/zero_mq_node_config.pb.h"


namespace goby
{
    void setup_socket(zmq::socket_t& socket, const goby::common::protobuf::ZeroMQServiceConfig::Socket& cfg);
    // run in the same thread as InterProcessPortal
    class ZMQMainThread
    {
    public:
	ZMQMainThread(zmq::context_t& context);
	bool non_blocking_recv(protobuf::InprocControl* control_msg);
      
    private:
	zmq::socket_t control_socket_;
	zmq::socket_t publish_socket_;
    };

    // run in a separate thread to allow zmq_.poll() to block without interrupting the main thread
    class ZMQReadThread
    {
    public:
	ZMQReadThread(const protobuf::InterProcessPortalConfig& cfg, zmq::context_t& context, std::atomic<bool>& alive);
	void run();
    private:
	void poll(long timeout_ms = -1);
	void control_data(const zmq::message_t& zmq_msg);
	void subscribe_data(const zmq::message_t& zmq_msg);
	void manager_data(const zmq::message_t& zmq_msg);
	
    private:
	const protobuf::InterProcessPortalConfig& cfg_;
	zmq::socket_t control_socket_;
	zmq::socket_t subscribe_socket_;
	zmq::socket_t manager_socket_;
	std::atomic<bool>& alive_;
	std::vector<zmq::pollitem_t> poll_items_;     
	enum { SOCKET_CONTROL = 0, SOCKET_MANAGER = 1, SOCKET_SUBSCRIBE = 2};
	enum { NUMBER_SOCKETS = 3 };
	bool have_pubsub_sockets_{false};
    };
    
    template<typename InnerTransporter = NullTransporter>
        class InterProcessPortal : public InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter>
        {
        public:
        using Base = InterProcessTransporterBase<InterProcessPortal<InnerTransporter>, InnerTransporter>;

        InterProcessPortal(const protobuf::InterProcessPortalConfig& cfg) :
	cfg_(cfg),
	zmq_context_(cfg.zeromq_number_io_threads()),
	zmq_main_(zmq_context_),
	zmq_read_thread_(cfg_, zmq_context_, zmq_alive_)
        { _init(); }

        InterProcessPortal(InnerTransporter& inner, const protobuf::InterProcessPortalConfig& cfg) :
	Base(inner),
	cfg_(cfg),
	zmq_context_(cfg.zeromq_number_io_threads()),
	zmq_main_(zmq_context_),
	zmq_read_thread_(zmq_context_, zmq_alive_)	
        { _init(); }

        ~InterProcessPortal()
        {
            zmq_alive_ = false;
            if(zmq_thread_)
                zmq_thread_->join();
        }
        
        
        
        friend Base;
        private:
	void _init()
	{
            using goby::protobuf::SerializerTransporterData;
            Base::inner_.template subscribe<Base::forward_group_, SerializerTransporterData>([this](std::shared_ptr<const SerializerTransporterData> d) { _receive_publication_forwarded(d);});
            
            Base::inner_.template subscribe<Base::forward_group_, SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>([this](std::shared_ptr<const SerializationSubscriptionBase> s) { _receive_subscription_forwarded(s); });


            // start zmq read thread
	    zmq_thread_.reset(new std::thread([this](){ zmq_read_thread_.run(); }));
	}

        
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

	    //            zmq_.send(msg, SOCKET_PUBLISH);
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
	    //            zmq_.subscribe(identifier, SOCKET_SUBSCRIBE);
        }
        
        int _poll()
        {
            int items = 0;
	    protobuf::InprocControl control_msg;
	    while(zmq_main_.non_blocking_recv(&control_msg))
	    {
		switch(control_msg.type())
		{
		case protobuf::InprocControl::RECEIVE:
		    ++items;
		    break;
		case protobuf::InprocControl::PUB_CONFIGURATION:
		    break;
		default: break;
		}
	    }		
            return items;
        }
        
       
        void _receive_publication_forwarded(std::shared_ptr<const goby::protobuf::SerializerTransporterData> data)
        {
            std::string identifier = _make_identifier(data->type(), data->marshalling_scheme(), data->group(), IdentifierWildcard::NO_WILDCARDS) + '\0';
            const auto& bytes = data->data();
            zmq::message_t msg(identifier.size() + bytes.size());
            memcpy(msg.data(), identifier.data(), identifier.size());
            memcpy(static_cast<char*>(msg.data())+identifier.size(),
                   bytes.data(), bytes.size());
                        
	    //            zmq_.send(msg, SOCKET_PUBLISH);
        }

        void _receive_subscription_forwarded(std::shared_ptr<const SerializationSubscriptionBase> subscription)
        {
            std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(), subscription->subscribed_group(), IdentifierWildcard::PROCESS_THREAD_WILDCARD);

            subscriptions_.insert(std::make_pair(identifier, subscription));
	    //            zmq_.subscribe(identifier, SOCKET_SUBSCRIBE);
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

	std::unique_ptr<std::thread> zmq_thread_;
        std::atomic<bool> zmq_alive_{true};
	zmq::context_t zmq_context_;
	ZMQMainThread zmq_main_;
	ZMQReadThread zmq_read_thread_;       
                
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

