#ifndef TransportCommon20160607H
#define TransportCommon20160607H

#include <memory>
#include <unordered_map>
#include <chrono>

#include "goby/util/binary.h"

#include "serialize_parse.h"

#include "goby/sandbox/protobuf/transporter_config.pb.h"

namespace goby
{

    class NoOpTransporter
    {
    public:
        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(const DataType& data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { }

        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(std::shared_ptr<DataType> data,
                         const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { }

        template<typename DataType, int scheme = scheme<DataType>()>
            void subscribe(const std::string& group, std::function<void(const DataType&)> func)
            { }

        template<typename DataType, int scheme = scheme<DataType>()>
            void subscribe(const std::string& group, std::function<void(std::shared_ptr<const DataType>)> func)
            { }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
            void subscribe(const std::string& group, void(C::*mem_func)(const DataType&), C* c)
            { }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
            void subscribe(const std::string& group, void(C::*mem_func)(std::shared_ptr<const DataType>), C* c)
            { }


        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        { return 0; }
        
        int poll(std::chrono::system_clock::duration wait_for)
        { return 0; }
        
    };
    


    template<typename InnerTransporter = NoOpTransporter>
        class SlowLinkTransporter
        {
        public:
       
        SlowLinkTransporter() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
        SlowLinkTransporter(InnerTransporter& inner) : inner_(inner) { }
        ~SlowLinkTransporter() { }

        template<typename DCCLMessage>
        void publish(const DCCLMessage& data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            _publish<DCCLMessage>(data, group, transport_cfg);
            inner_.publish<DCCLMessage, MarshallingScheme::DCCL>(data, group, transport_cfg);
        }

        template<typename DCCLMessage>
        void publish(std::shared_ptr<DCCLMessage> data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            if(data)
            {
                _publish<DCCLMessage>(*data, group, transport_cfg);
                inner_.publish<DCCLMessage, MarshallingScheme::DCCL>(data, group, transport_cfg);
            }
        }
            
        private:
        template<typename DCCLMessage>
        void _publish(const DCCLMessage& data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            static_assert(scheme<DCCLMessage>() == MarshallingScheme::DCCL, "Can only use DCCL messages with SlowLinkTransporter");
            
            std::vector<char> bytes(SerializerParserHelper<DCCLMessage, MarshallingScheme::DCCL>::serialize(data));
            std::cout << "SlowLinkTransporter: Publishing to group [" << group << "], using scheme [" << MarshallingScheme::as_string(MarshallingScheme::DCCL) << "]: " << goby::util::hex_encode(std::string(bytes.begin(), bytes.end())) << std::endl;
        }

        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        
        };
}

#endif

