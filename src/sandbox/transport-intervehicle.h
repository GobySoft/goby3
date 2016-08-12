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
#include "goby/sandbox/protobuf/intervehicle_transporter_config.pb.h"

namespace goby
{   
    template<typename InnerTransporter, typename GroupType = int>
        class InterVehicleTransporter : public SerializationTransporterBase<InnerTransporter, GroupType>
        {
        public:
            typedef GroupType Group;
            
        InterVehicleTransporter(InnerTransporter& inner) : SerializationTransporterBase<InnerTransporter, Group>(inner) { }
            ~InterVehicleTransporter() { }

            static const std::string forward_group;
        private:
            const std::string& forward_group_name() override { return forward_group; }
        };

    template<typename InnerTransport, typename GroupType>
        const std::string InterVehicleTransporter<InnerTransport, GroupType>::forward_group( "goby::InterVehicleTransporter");
    
    

    template<typename InnerTransporter = NoOpTransporter, typename GroupType = int>
        class SlowLinkTransporter
        {
        public:
        typedef typename InterVehicleTransporter<InnerTransporter, GroupType>::Group Group;

        SlowLinkTransporter(const protobuf::SlowLinkTransporterConfig& cfg) : own_inner_(new InnerTransporter), inner_(*own_inner_), cfg_(cfg) { _init(); }
        SlowLinkTransporter(InnerTransporter& inner, const protobuf::SlowLinkTransporterConfig& cfg) : inner_(inner), cfg_(cfg) { _init(); }
        ~SlowLinkTransporter() { }

        template<typename Data>
        void publish(const Data& data,
                     const Group& group = Group(),
                     const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            _publish<Data>(data, group, transport_cfg);
            inner_.publish<Data, MarshallingScheme::DCCL>(data, group_convert(group), transport_cfg);
        }

        template<typename Data>
        void publish(std::shared_ptr<Data> data,
                     const Group& group = Group(),
                     const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            if(data)
            {
                _publish<Data>(*data, group, transport_cfg);
                inner_.publish<Data, MarshallingScheme::DCCL>(data, group_convert(group), transport_cfg);
            }
        }


        // direct subscriptions (possibly without an "InnerTransporter")
        template<typename Data>
        void subscribe(std::function<void(const Data&)> func,
                       const Group& group = Group(),
                       std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
        {
            inner_.subscribe<Data, MarshallingScheme::DCCL>(func, group_convert(group));
            _subscribe<Data>([=](std::shared_ptr<const Data> d) { func(*d); }, group, group_func);
        }
        
        template<typename Data>
        void subscribe(std::function<void(std::shared_ptr<const Data>)> func,
                       const Group& group = Group(),
                       std::function<Group(const Data&)> group_func = [](const Data&) { return Group(); })
        {
            inner_.subscribe<Data, MarshallingScheme::DCCL>(func, group_convert(group));
            _subscribe<Data>(func, group, group_func);
        }

        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            return poll(std::chrono::system_clock::now() + wait_for);
        }
    
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            auto now = std::chrono::system_clock::time_point::min();
            int items = 0;
            received_items_ = 0;
            while(items == 0 && now < timeout)
            {
                // run at 10Hz
                items += inner_.poll(std::chrono::milliseconds(100));
                driver_->do_work();
                mac_.do_work();
                q_manager_.do_work();
                items += received_items_;
                now = std::chrono::system_clock::now();
            }
            return items;
        }
        
        private:
        template<typename Data>
        void _publish(const Data& data,
                      const Group& group,
                      const goby::protobuf::TransporterConfig& transport_cfg)
        {
            static_assert(scheme<Data>() == MarshallingScheme::DCCL, "Can only use DCCL messages with SlowLinkTransporter");
            
            std::vector<char> bytes(SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(data));
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

            inner_.subscribe<goby::protobuf::SerializerTransporterData>([this](const goby::protobuf::SerializerTransporterData& d) { _receive_publication_forwarded(d); }, InterVehicleTransporter<InnerTransporter, GroupType>::forward_group);

            
            inner_.subscribe<goby::protobuf::InterVehicleSubscription>([this](const goby::protobuf::InterVehicleSubscription& d) { _receive_subscription_forwarded(d); },InterVehicleTransporter<InnerTransporter, GroupType>::forward_group);

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
            
            std::cout << "Received: " << rx_msg.ShortDebugString() << std::endl;
        }        
        
        void _receive_publication_forwarded(const goby::protobuf::SerializerTransporterData& data)
        {

        }

        void _receive_subscription_forwarded(const goby::protobuf::InterVehicleSubscription& subscription)
        {
            
        }


        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;

        const goby::protobuf::SlowLinkTransporterConfig& cfg_;
        
        // maps DCCL ID to map of Group->subscription
        std::unordered_map<int, std::unordered_multimap<std::string, std::shared_ptr<const SerializationSubscriptionBase>>> subscriptions_;

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
