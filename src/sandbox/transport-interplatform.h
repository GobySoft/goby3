#ifndef TransportInterPlatform20160810H
#define TransportInterPlatform20160810H

#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <thread>
#include <atomic>

#include "goby/common/zeromq_service.h"

#include "goby/acomms/queue.h"
#include "goby/acomms/modem_driver.h"
#include "goby/acomms/amac.h"
#include "goby/acomms/bind.h"

#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"

#include "transport-common.h"
#include "goby/sandbox/protobuf/intervehicle_transporter_config.pb.h"

namespace goby
{
   template<typename Derived, typename InnerTransporter, typename Group>
        class InterPlatformTransporterBase
    {
    public:        
    InterPlatformTransporterBase(InnerTransporter& inner) : inner_(inner) { }
    InterPlatformTransporterBase() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
            
        template<typename Data>
            void publish(const Data& data, const Group& group = Group(), const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterPlatformTransporters");
                static_cast<Derived*>(this)->template _publish<Data>(data, group, transport_cfg);
                inner_.publish<Data, scheme<Data>()>(data, group_convert(group), transport_cfg);
            }

        template<typename Data>
            void publish(std::shared_ptr<Data> data, const Group& group = Group(), const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterPlatformTransporters");
                if(data)
                {
                    static_cast<Derived*>(this)->template _publish<Data>(*data, group, transport_cfg);
                    inner_.publish<Data, scheme<Data>()>(data, group_convert(group), transport_cfg);
                }
            }


        template<typename Data>
            void subscribe(std::function<void(const Data&)> func,
                           const Group& group = Group(),
                           std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
            {
                static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterPlatformTransporters");
                inner_.subscribe<Data, scheme<Data>()>(func, group_convert(group));
                auto pointer_ref_lambda = [=](std::shared_ptr<const Data> d) { func(*d); };
                static_cast<Derived*>(this)->template _subscribe<Data>(pointer_ref_lambda, group, group_func);
            }
        
        template<typename Data>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> func,
                           const Group& group = Group(),
                           std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
            {
                static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterPlatformTransporters");
                inner_.subscribe<Data, scheme<Data>()>(func, group_convert(group));
                static_cast<Derived*>(this)->template _subscribe<Data>(func, group, group_func);
            }

        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return static_cast<Derived*>(this)->_poll(timeout);
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            return poll(std::chrono::system_clock::now() + wait_for);
        }

        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        const std::string forward_group_ { "goby::InterPlatformTransporter" };
    };    
    
    
    template<typename InnerTransporter>
        class InterPlatformTransporter : public InterPlatformTransporterBase<InterPlatformTransporter<InnerTransporter>, InnerTransporter, int>
    {
    public:
        using Group = int;
        using Base = InterPlatformTransporterBase<InterPlatformTransporter<InnerTransporter>, InnerTransporter, Group>;

    InterPlatformTransporter(InnerTransporter& inner) : Base(inner)
        {
            Base::inner_.template subscribe<goby::protobuf::DCCLForwardedData>([this](const goby::protobuf::DCCLForwardedData& d) { _receive_dccl_data_forwarded(d); }, Base::forward_group_);
        }

    friend Base;
    private:
        template<typename Data>
            void _publish(const Data& d, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            // create and forward publication to edge
            std::vector<char> bytes(SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(d));
            std::string* sbytes = new std::string(bytes.begin(), bytes.end());
            std::shared_ptr<goby::protobuf::SerializerTransporterData> data = std::make_shared<goby::protobuf::SerializerTransporterData>();

            data->set_marshalling_scheme(MarshallingScheme::DCCL);
            data->set_type(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name(d));
            data->set_group(group_convert(group));
            data->set_allocated_data(sbytes);
        
            *data->mutable_cfg() = transport_cfg;

            Base::inner_.publish(data, Base::forward_group_);
        }

    
        template<typename Data>
            void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func,
                            const Group& group,
                            std::function<Group(const Data&)> group_func)
        {
            int dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::codec().template id<Data>();
            auto subscribe_lambda = [=](std::shared_ptr<Data> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { func(d); };
            typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function,
                                                                             group_convert(group),
                                                                             [=](const Data&d) { return group_convert(group_func(d)); })); 
            
            subscriptions_[dccl_id].insert(std::make_pair(group_convert(group), subscription));
                    
            goby::protobuf::DCCLSubscription dccl_subscription;
            dccl_subscription.set_dccl_id(dccl_id);
            dccl_subscription.set_group(group);
            Base::inner_.template publish<goby::protobuf::DCCLSubscription>(dccl_subscription, Base::forward_group_);
        }
    
        int _poll(const std::chrono::system_clock::time_point& timeout)
        {
            return Base::inner_.poll(timeout);
        }
    
        void _receive_dccl_data_forwarded(const goby::protobuf::DCCLForwardedData& d)
        {
            std::cout << "InterPlatformTransporter received forwarded data: " << d.DebugString() << std::endl;

            for(auto& frame: d.frame())
            {
                std::string::const_iterator frame_it = frame.begin(), frame_end = frame.end();
                while(frame_it < frame_end)
                {
                    auto dccl_id = DCCLSerializerParserHelperBase::codec().id(frame_it, frame_end);
                    std::string::const_iterator next_frame_it;
                    for(auto p : subscriptions_[dccl_id])
                        next_frame_it = p.second->post(frame_it, frame_end);
                    
                    frame_it = next_frame_it;
                }
            }
        }
    
    private:
        std::unordered_map<int, std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>>> subscriptions_;
    
    };

    
    template<typename InnerTransporter = NoOpTransporter>
    class SlowLinkTransporter : public InterPlatformTransporterBase<SlowLinkTransporter<InnerTransporter>, InnerTransporter, int>
        {
        public:
        using Group = int;
        using Base = InterPlatformTransporterBase<SlowLinkTransporter<InnerTransporter>, InnerTransporter, Group>;

        SlowLinkTransporter(const protobuf::SlowLinkTransporterConfig& cfg) : cfg_(cfg) { _init(); }
        SlowLinkTransporter(InnerTransporter& inner, const protobuf::SlowLinkTransporterConfig& cfg) : Base(inner), cfg_(cfg) { _init(); }

        friend Base;
        private:
        
        template<typename Data>
        void _publish(const Data& data,
                      const Group& group,
                      const goby::protobuf::TransporterConfig& transport_cfg)
        {
            
            std::vector<char> bytes(SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(data));
            // TODO: should be able to push bytes directly
            q_manager_.push_message(data);
        }

        template<typename Data>
        void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func,
                        const Group& group,
                        std::function<Group(const Data&)> group_func)
        {
            int dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::codec().template id<Data>();
            auto subscribe_lambda = [=](std::shared_ptr<Data> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { func(d); };
            typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function,
                                                                             group_convert(group),
                                                                             [=](const Data&d) { return group_convert(group_func(d)); })); 
            
            subscriptions_[dccl_id].insert(std::make_pair(group_convert(group), subscription));
        }
    
        int _poll(const std::chrono::system_clock::time_point& timeout)
        {
            auto now = std::chrono::system_clock::time_point::min();
            int items = 0;
            received_items_ = 0;
            while(items == 0 && now < timeout)
            {
                // run at 10Hz
                items += Base::inner_.poll(std::chrono::milliseconds(100));
                driver_->do_work();
                mac_.do_work();
                q_manager_.do_work();
                items += received_items_;
                now = std::chrono::system_clock::now();
            }
            return items;
        }
        
        void _init()
        {
            switch(cfg_.driver_type())
            {
                case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
                driver_.reset(new goby::acomms::MMDriver);
                break;

                case goby::acomms::protobuf::DRIVER_IRIDIUM:
                driver_.reset(new goby::acomms::IridiumDriver);
                break;
            
                case goby::acomms::protobuf::DRIVER_UDP:
                asio_service_.push_back(std::unique_ptr<boost::asio::io_service>(
                                            new boost::asio::io_service));
                driver_.reset(new goby::acomms::UDPDriver(asio_service_.back().get()));
                break;

                case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
                driver_.reset(new goby::acomms::IridiumShoreDriver);
                break;
            
                case goby::acomms::protobuf::DRIVER_NONE: break;

                default:
                throw(std::runtime_error("Unsupported driver type: " + goby::acomms::protobuf::DriverType_Name(cfg_.driver_type())));
                break;
            }

            goby::acomms::bind(*driver_, q_manager_, mac_);
    
            driver_->signal_receive.connect([&](const goby::acomms::protobuf::ModemTransmission& rx_msg) { this->_receive(rx_msg); });

            Base::inner_.template subscribe<goby::protobuf::SerializerTransporterData>([this](const goby::protobuf::SerializerTransporterData& d) { _receive_publication_forwarded(d); }, Base::forward_group_);

            
            Base::inner_.template subscribe<goby::protobuf::DCCLSubscription>([this](const goby::protobuf::DCCLSubscription& d) { _receive_subscription_forwarded(d); },Base::forward_group_);

            q_manager_.set_cfg(cfg_.queue_cfg());
            mac_.startup(cfg_.mac_cfg());
            driver_->startup(cfg_.driver_cfg());

        }
        
        void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
        {            
            for(auto& frame: rx_msg.frame())
            {
                std::string::const_iterator frame_it = frame.begin(), frame_end = frame.end();
                while(frame_it < frame_end)
                {
                    auto dccl_id = DCCLSerializerParserHelperBase::codec().id(frame_it, frame_end);
                    std::string::const_iterator next_frame_it;
                    for(auto p : subscriptions_[dccl_id])
                        next_frame_it = p.second->post(frame_it, frame_end);

                    frame_it = next_frame_it;
                    ++received_items_;
                }
            }

            // unless we want to require the edge to have all the DCCL messages loaded,
            // all we can do is forwarded the entire data to the InterPlatformTransporters to parse
            if(forwarded_subscriptions_.size() > 0)
            {
                goby::protobuf::DCCLForwardedData data;
                for(auto& frame: rx_msg.frame())
                    *data.add_frame() = frame;
                Base::inner_.publish(data, Base::forward_group_);
            }
                    
            std::cout << "Received: " << rx_msg.ShortDebugString() << std::endl;
        }        
        
        void _receive_publication_forwarded(const goby::protobuf::SerializerTransporterData& data)
        {
            // TODO: should be able to push bytes directly
            std::unique_ptr<google::protobuf::Message> new_msg = DCCLSerializerParserHelperBase::codec().decode<std::unique_ptr<google::protobuf::Message>>(data.data());
            q_manager_.push_message(*new_msg);
        }

        void _receive_subscription_forwarded(const goby::protobuf::DCCLSubscription& dccl_subscription)
        {
            std::cout << "SlowLinkTransporter received forwarded subscription: " << dccl_subscription.DebugString() << std::endl;
            
            auto group = dccl_subscription.group();          
            forwarded_subscriptions_[dccl_subscription.dccl_id()].insert(std::make_pair(group_convert(group), dccl_subscription));            
        }        
        
        const goby::protobuf::SlowLinkTransporterConfig& cfg_;
        
        // maps DCCL ID to map of Group->subscription
        std::unordered_map<int, std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>>> subscriptions_;
        std::unordered_map<int, std::unordered_multimap<std::string, goby::protobuf::DCCLSubscription>> forwarded_subscriptions_;

        goby::acomms::QueueManager q_manager_;

        // this needs to be a shared ptr to play nice with Boost at destruction...not sure why.
        std::shared_ptr<goby::acomms::ModemDriverBase> driver_;
        
        // for PBDriver
        std::vector<std::unique_ptr<goby::common::ZeroMQService> > zeromq_service_;
        // for UDPDriver
        std::vector<std::unique_ptr<boost::asio::io_service> > asio_service_;

        goby::acomms::MACManager mac_;

        int received_items_{0};
        };
}


#endif
