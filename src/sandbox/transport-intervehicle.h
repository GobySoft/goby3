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
    template<typename InnerTransporter>
        class InterVehicleTransporter : public SerializationTransporterBase<InnerTransporter, int>
        {
        public:
            typedef int Group;
            
        InterVehicleTransporter(InnerTransporter& inner) : SerializationTransporterBase<InnerTransporter, Group>(inner) { }
            ~InterVehicleTransporter() { }

            static const std::string forward_group;
        private:
            const std::string& forward_group_name() override { return forward_group; }
        };

    template<typename InnerTransport>
        const std::string InterVehicleTransporter<InnerTransport>::forward_group( "goby::InterVehicleTransporter");
    
    

    template<typename InnerTransporter = NoOpTransporter>
        class SlowLinkTransporter
        {
        public:
        typedef typename InterVehicleTransporter<InnerTransporter>::Group Group;

        SlowLinkTransporter(const protobuf::SlowLinkTransporterConfig& cfg) : own_inner_(new InnerTransporter), inner_(*own_inner_), cfg_(cfg) { _init(); }
        SlowLinkTransporter(InnerTransporter& inner, const protobuf::SlowLinkTransporterConfig& cfg) : inner_(inner), cfg_(cfg) { _init(); }
        ~SlowLinkTransporter() { }

        template<typename DCCLMessage>
        void publish(const DCCLMessage& data,
                     const Group& group,
                     const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            _publish<DCCLMessage>(data, group, transport_cfg);
            inner_.publish<DCCLMessage, MarshallingScheme::DCCL>(data, group_convert(group), transport_cfg);
        }

        template<typename DCCLMessage>
        void publish(std::shared_ptr<DCCLMessage> data,
                     const Group& group,
                     const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            if(data)
            {
                _publish<DCCLMessage>(*data, group, transport_cfg);
                inner_.publish<DCCLMessage, MarshallingScheme::DCCL>(data, group_convert(group), transport_cfg);
            }
        }

        // direct subscriptions (possibly without an "InnerTransporter")
        template<typename DCCLMessage>
        void subscribe(const Group& group,
                       std::function<void(const DCCLMessage&)> func)
        {
            inner_.subscribe<DCCLMessage, MarshallingScheme::DCCL>(group_convert(group), func);
            int dccl_id = SerializerParserHelper<DCCLMessage, MarshallingScheme::DCCL>::codec().template id<DCCLMessage>();
            
            auto subscribe_lambda = [&](std::shared_ptr<DCCLMessage> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { func(*d); };
            typename SerializationSubscription<DCCLMessage, MarshallingScheme::DCCL>::HandlerType subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationSubscriptionBase>(new SerializationSubscription<DCCLMessage, MarshallingScheme::DCCL>(subscribe_function, group_convert(group)));
            
            subscriptions_.insert(std::make_pair(dccl_id, subscription));
        }

        template<typename DataType, int scheme = scheme<DataType>(), class C>
        void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
        {
            subscribe<DataType, scheme>(group, [=](const DataType& d) { (c->*mem_func)(d); });
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
        template<typename DCCLMessage>
        void _publish(const DCCLMessage& data,
                      const Group& group,
                      const goby::protobuf::TransporterConfig& transport_cfg)
        {
            static_assert(scheme<DCCLMessage>() == MarshallingScheme::DCCL, "Can only use DCCL messages with SlowLinkTransporter");
            
            std::vector<char> bytes(SerializerParserHelper<DCCLMessage, MarshallingScheme::DCCL>::serialize(data));
            std::cout << "SlowLinkTransporter: Publishing to group [" << group << "], using scheme [" << MarshallingScheme::as_string(MarshallingScheme::DCCL) << "]: " << goby::util::hex_encode(std::string(bytes.begin(), bytes.end())) << std::endl;

            q_manager_.push_message(data);
        }

        private:

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

            //inner_.subscribe<goby::protobuf::SerializerTransporterData>(InterVehicleTransporter<InnerTransporter>::forward_group, &SlowLinkTransporter::_receive_publication_forwarded, this);
            inner_.subscribe<goby::protobuf::SerializerTransporterData>(InterVehicleTransporter<InnerTransporter>::forward_group,
                                                                        [this](const goby::protobuf::SerializerTransporterData& d) { this->_receive_publication_forwarded(d); });

            
            inner_.subscribe<goby::protobuf::InterVehicleSubscription>(InterVehicleTransporter<InnerTransporter>::forward_group, &SlowLinkTransporter::_receive_subscription_forwarded, this);

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
                    auto p = subscriptions_.equal_range(dccl_id);
                    for (auto sub_it = p.first; sub_it != p.second; ++sub_it)
                    {
                        ++received_items_;
                        frame_it = sub_it->second->post(frame_it, frame_end);
                    }
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
        
        // maps DCCL ID to subscription
        std::unordered_map<int, std::shared_ptr<const SerializationSubscriptionBase>> subscriptions_;

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
