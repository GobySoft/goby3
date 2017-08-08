#ifndef TransportInterProcess20160622H
#define TransportInterProcess20160622H

#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <thread>
#include <atomic>
#include <tuple>

#include "transport-common.h"
#include "goby/middleware/protobuf/interprocess_config.pb.h"


namespace goby
{   
    template<typename Derived, typename InnerTransporter>
        class InterProcessTransporterBase :
        public StaticTransporterInterface<InterProcessTransporterBase<Derived, InnerTransporter>, InnerTransporter>,
        public PollRelativeTimeInterface<InterProcessTransporterBase<Derived, InnerTransporter>>
    {
    public:        
    InterProcessTransporterBase(InnerTransporter& inner) : inner_(inner) { }
    InterProcessTransporterBase() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
        
        // RUNTIME groups
        template<typename Data, int scheme = scheme<Data>()>
            void publish_dynamic(const Data& data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity_runtime(group);
                static_cast<Derived*>(this)->template _publish<Data, scheme>(data, group, transport_cfg);
                inner_.template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }

        template<typename Data, int scheme = scheme<Data>()>
            void publish_dynamic(std::shared_ptr<Data> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                if(data)
                {
                    check_validity_runtime(group);
                    static_cast<Derived*>(this)->template _publish<Data, scheme>(*data, group, transport_cfg);
                    inner_.template publish_dynamic<Data, scheme>(data, group, transport_cfg);
                }
            }        
        
        template<typename Data, int scheme = scheme<Data>()>
            void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group)
        {
            check_validity_runtime(group);
            inner_.template subscribe_dynamic<Data, scheme>(f, group);
            static_cast<Derived*>(this)->template _subscribe<Data, scheme>([=](std::shared_ptr<const Data> d) { f(*d); }, group);
        }
        
        template<typename Data, int scheme = scheme<Data>()>
            void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group)
        {
            check_validity_runtime(group);
            inner_.template subscribe_dynamic<Data, scheme>(f, group);
            static_cast<Derived*>(this)->template _subscribe<Data, scheme>(f, group);
        }

        // Wildcards
        //void subscribe_wildcard(std::function<void(const std::vector<unsigned char>&)> f, );
        
        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        static constexpr Group forward_group_ { "goby::InterProcessForwarder" };

        friend PollRelativeTimeInterface<InterProcessTransporterBase<Derived, InnerTransporter>>;
    private:  
        int _poll(std::chrono::system_clock::duration wait_for)
        {
            return static_cast<Derived*>(this)->_poll(wait_for);
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
        template<typename Data, int scheme>
            void _publish(const Data& d, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
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
        
        template<typename Data, int scheme>
            void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f, const Group& group)
        {
            // forward subscription to edge
            auto inner_publication_lambda = [&](std::shared_ptr<Data> d, const goby::protobuf::TransporterConfig& t) { Base::inner_.template publish_dynamic<Data, scheme>(d, group, t); };
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

}

#endif
