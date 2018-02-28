// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef TransportInterVehicle20160810H
#define TransportInterVehicle20160810H

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
#include "goby/middleware/protobuf/interplatform_config.pb.h"

namespace goby
{
   template<typename Derived, typename InnerTransporter>
       class InterVehicleTransporterBase :
       public StaticTransporterInterface<InterVehicleTransporterBase<Derived, InnerTransporter>, InnerTransporter>,
       public Poller<InterVehicleTransporterBase<Derived, InnerTransporter>>
    {
        using PollerType = Poller<InterVehicleTransporterBase<Derived, InnerTransporter>>;

    public:        
    InterVehicleTransporterBase(InnerTransporter& inner) :
        PollerType(&inner), inner_(inner) { }
    InterVehicleTransporterBase(InnerTransporter* inner_ptr = new InnerTransporter,
                                bool base_owns_inner = true) :
        PollerType(inner_ptr),
            own_inner_(base_owns_inner ? inner_ptr : nullptr),
            inner_(*inner_ptr)
            { }

	template<typename Data>
	    static constexpr int scheme()
	{
	    return goby::scheme<Data>();
	}

	
        template<typename Data>
            void publish(const Data& data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { publish_dynamic<Data>(data, Group(), transport_cfg); }

        template<typename Data>
            void publish(std::shared_ptr<const Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { publish_dynamic<Data>(data, Group(), transport_cfg); }
        
        template<typename Data>
            void publish(std::shared_ptr<Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        { publish<Data>(std::shared_ptr<const Data>(data), transport_cfg); }
        
        template<typename Data>
            void subscribe(std::function<void(const Data&)> func, std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
        {
            subscribe_dynamic<Data>(func, Group(), group_func);
        }
        
        template<typename Data>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> func,
                           std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
        {
            subscribe_dynamic<Data>(func, Group(), group_func);
        }
        
        template<typename Data>
            void publish_dynamic(const Data& data, const Group& group = Group(), const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterVehicleTransporters");
            static_cast<Derived*>(this)->template _publish<Data>(data, group, transport_cfg);
            inner_.template publish_dynamic<Data, scheme<Data>()>(data, group, transport_cfg);
        }

        template<typename Data>
            void publish_dynamic(std::shared_ptr<const Data> data, const Group& group = Group(), const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterVehicleTransporters");
            if(data)
            {
                static_cast<Derived*>(this)->template _publish<Data>(*data, group, transport_cfg);
                inner_.template publish_dynamic<Data, scheme<Data>()>(data, group, transport_cfg);
            }
        }

        
        template<typename Data>
            void publish_dynamic(std::shared_ptr<Data> data, const Group& group = Group(), const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            publish_dynamic<Data>(std::shared_ptr<const Data>(data), group, transport_cfg);
        }
        
        
        template<typename Data>
            void subscribe_dynamic(std::function<void(const Data&)> func,
                                   const Group& group = Group(),
                                   std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
        {
            static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterVehicleTransporters");
            auto pointer_ref_lambda = [=](std::shared_ptr<const Data> d) { func(*d); };
            static_cast<Derived*>(this)->template _subscribe<Data>(pointer_ref_lambda, group, group_func);
        }
        
        template<typename Data>
            void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> func,
                                   const Group& group = Group(),
                                   std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
        {
            static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with InterVehicleTransporters");
            static_cast<Derived*>(this)->template _subscribe<Data>(func, group, group_func);
        }        

        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        static constexpr Group forward_group_ { "goby::InterVehicleTransporter" };

    private:
        friend PollerType;        
        int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
        { return static_cast<Derived*>(this)->_poll(lock); }
    };    

   template<typename Derived, typename InnerTransporter>
       constexpr goby::Group InterVehicleTransporterBase<Derived, InnerTransporter>::forward_group_;
   
    
    template<typename InnerTransporter>
        class InterVehicleForwarder : public InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>
    {
    public:
        using Base = InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>;

    InterVehicleForwarder(InnerTransporter& inner) : Base(inner)
        {
            Base::inner_.template subscribe<Base::forward_group_, goby::protobuf::DCCLForwardedData>([this](const goby::protobuf::DCCLForwardedData& d) { _receive_dccl_data_forwarded(d); });
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
            data->set_type(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
            data->set_group(std::string(group));
            data->set_allocated_data(sbytes);
        
            *data->mutable_cfg() = transport_cfg;

            Base::inner_.template publish<Base::forward_group_>(data);
        }

    
        template<typename Data>
            void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func,
                            const Group& group,
                            std::function<Group(const Data&)> group_func)
        {
            auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();
        
            
            auto subscribe_lambda = [=](std::shared_ptr<const Data> d, const goby::protobuf::TransporterConfig& t) { func(d); };
            typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function, group, group_func)); 

            subscriptions_[dccl_id].insert(std::make_pair(group, subscription));
                    
            goby::protobuf::DCCLSubscription dccl_subscription;
            dccl_subscription.set_dccl_id(dccl_id);
            dccl_subscription.set_group(group.numeric());
            dccl_subscription.set_protobuf_name(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
            _insert_file_desc_with_dependencies(Data::descriptor()->file(), &dccl_subscription);
            Base::inner_.template publish<Base::forward_group_, goby::protobuf::DCCLSubscription>(dccl_subscription);
        }


        // used to populated DCCLSubscription file_descriptor fields
        void _insert_file_desc_with_dependencies(const google::protobuf::FileDescriptor* file_desc, protobuf::DCCLSubscription* subscription)
        {
            for(int i = 0, n = file_desc->dependency_count(); i < n; ++i)
                _insert_file_desc_with_dependencies(file_desc->dependency(i), subscription);

            google::protobuf::FileDescriptorProto* file_desc_proto = subscription->add_file_descriptor();
            file_desc->CopyTo(file_desc_proto);
        }

        

        int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
        { return 0; }
    
        void _receive_dccl_data_forwarded(const goby::protobuf::DCCLForwardedData& packets)
        {
            for(const auto& packet : packets.frame())
            {
                for(auto p : subscriptions_[packet.dccl_id()])
                    p.second->post(packet.data().begin(), packet.data().end());
            }
        }
    
    private:
        std::unordered_map<int, std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>>> subscriptions_;
    
    };

    class ModemDriverThread
    {
    public:
        ModemDriverThread(const protobuf::InterVehiclePortalConfig& cfg, std::atomic<bool>& alive, std::shared_ptr<std::condition_variable_any> poller_cv);
	void run();
        void publish(const std::string& bytes);
        bool retrieve_message(goby::acomms::protobuf::ModemTransmission* msg);
        
    private:
        void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg);
        void _data_request(goby::acomms::protobuf::ModemTransmission* msg);

    private:
        std::mutex mutex_;
        const protobuf::InterVehiclePortalConfig& cfg_;
        std::atomic<bool>& alive_;
        std::shared_ptr<std::condition_variable_any> poller_cv_;

        std::deque<std::string> sending_;
        std::deque<goby::acomms::protobuf::ModemTransmission> received_;
        
        // goby::acomms::QueueManager q_manager_;

        // for UDPDriver
        std::vector<std::unique_ptr<boost::asio::io_service> > asio_service_;
        std::unique_ptr<goby::acomms::ModemDriverBase> driver_;
        

        goby::acomms::MACManager mac_;
    };
    
    
    template<typename InnerTransporter = NullTransporter>
    class InterVehiclePortal : public InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>
        {
        public:
        using Base = InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>;

        InterVehiclePortal(const protobuf::InterVehiclePortalConfig& cfg) :
        cfg_(cfg),
        driver_thread_(cfg, driver_thread_alive_, PollerInterface::cv())
        { _init(); }
        InterVehiclePortal(InnerTransporter& inner, const protobuf::InterVehiclePortalConfig& cfg) :
        Base(inner),
        cfg_(cfg),
        driver_thread_(cfg, driver_thread_alive_, PollerInterface::cv())
        { _init(); }

        ~InterVehiclePortal()
        {
            driver_thread_alive_ = false;
            if(modem_driver_thread_)
                modem_driver_thread_->join();
        }
        
            
        friend Base;
        private:
        
        template<typename Data>
        void _publish(const Data& data,
                      const Group& group,
                      const goby::protobuf::TransporterConfig& transport_cfg)
        {
            
            std::vector<char> bytes(SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(data));
            driver_thread_.publish(std::string(bytes.begin(), bytes.end()));
        }

        template<typename Data>
        void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func,
                        const Group& group,
                        std::function<Group(const Data&)> group_func)
        {
            auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();
            
            auto subscribe_lambda = [=](std::shared_ptr<const Data> d, const goby::protobuf::TransporterConfig& t) { func(d); };
            typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
                new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function, group, group_func));
            
            subscriptions_[dccl_id].insert(std::make_pair(group, subscription));
        }
    
        int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
        {
            int items = 0;
            goby::acomms::protobuf::ModemTransmission msg;
            while(driver_thread_.retrieve_message(&msg))
            {
                _receive(msg);
                ++items;
		if(lock) lock.reset();
            }
            return items;
        }
        
        void _init()
        {

            Base::inner_.template subscribe<Base::forward_group_, goby::protobuf::SerializerTransporterData>([this](const goby::protobuf::SerializerTransporterData& d) { _receive_publication_forwarded(d); });         
            Base::inner_.template subscribe<Base::forward_group_, goby::protobuf::DCCLSubscription>([this](const goby::protobuf::DCCLSubscription& d) { _receive_subscription_forwarded(d); });
            modem_driver_thread_.reset(new std::thread([this]() { driver_thread_.run(); }));

        }
        
        void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
        {
            
            for(auto& frame: rx_msg.frame())
            {
                const protobuf::DCCLForwardedData packets(DCCLSerializerParserHelperBase::unpack(frame));
                for(const auto& packet : packets.frame())
                {
                    for(auto p : subscriptions_[packet.dccl_id()])
                        p.second->post(packet.data().begin(), packet.data().end());
                }

                // forward to edges
                Base::inner_.template publish<Base::forward_group_>(packets);
            }
        }        
        
        void _receive_publication_forwarded(const goby::protobuf::SerializerTransporterData& data)
        {
            driver_thread_.publish(data.data());
        }

        void _receive_subscription_forwarded(const goby::protobuf::DCCLSubscription& dccl_subscription)
        {            
            auto group = dccl_subscription.group();
            forwarded_subscriptions_[dccl_subscription.dccl_id()].insert(std::make_pair(group, dccl_subscription));
            DCCLSerializerParserHelperBase::load_forwarded_subscription(dccl_subscription);
        }        
        
        const goby::protobuf::InterVehiclePortalConfig& cfg_;
        std::unique_ptr<std::thread> modem_driver_thread_;
        std::atomic<bool> driver_thread_alive_{true};
        ModemDriverThread driver_thread_;
        
        // maps DCCL ID to map of Group->subscription
        std::unordered_map<int, std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>>> subscriptions_;
        std::unordered_map<int, std::unordered_multimap<Group, goby::protobuf::DCCLSubscription>> forwarded_subscriptions_;


        };
}


#endif
