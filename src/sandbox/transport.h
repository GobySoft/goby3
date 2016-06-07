#ifndef Transport20160607H
#define Transport20160607H

#include <memory>

#include "goby/util/binary.h"

#include "serialize_parse.h"

namespace goby
{
    class TransporterConfig
    {
    public:
    TransporterConfig() : ttl_(64) {}   
    private:
        int ttl_;
    };
    
    
    template<typename T> constexpr bool False()  { return false; }

    class NoOpTransporter
    {
    public:
        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg)
        {
            std::cout << "NoOp const ref publish" << std::endl;
        }

        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(std::shared_ptr<DataType> data,
                         const std::string& group, const TransporterConfig& transport_cfg)
        {
            std::cout << "NoOp shared_ptr publish" << std::endl;
        }
    };
    
    
    template<typename InnerTransporter = NoOpTransporter>
        class ZMQTransporter
        {
        public:
        ZMQTransporter() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
        ZMQTransporter(InnerTransporter& inner) : inner_(inner) { }
        ~ZMQTransporter() { }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            _publish<DataType, scheme>(data, group, transport_cfg);
            inner_.publish<DataType, scheme>(data, group, transport_cfg);
        }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(std::shared_ptr<DataType> data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            if(data)
            {
                _publish<DataType, scheme>(*data, group, transport_cfg);
                inner_.publish<DataType, scheme>(data, group, transport_cfg);
            }
        }
            
        private:
        template<typename DataType, int scheme>
        void _publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<DataType, scheme>::serialize(data));
            std::cout << "ZMQTransporter: Publishing to group [" << group << "], using scheme [" << MarshallingScheme::as_string(scheme) << "]: " << goby::util::hex_encode(std::string(bytes.begin(), bytes.end())) << std::endl;
        }

        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        
        };

    class IntraProcessTransporter
    {
    public:
        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            std::cout << "IntraProcessTransporter const ref publish" << std::endl;

        }

        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(std::shared_ptr<DataType> data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            std::cout << "IntraProcessTransporter shared_ptr publish" << std::endl;
        }
    };    


    template<typename InnerTransporter = NoOpTransporter>
        class SlowLinkTransporter
        {
        public:
        SlowLinkTransporter() : own_inner_(new InnerTransporter), inner_(*own_inner_) { }
        SlowLinkTransporter(InnerTransporter& inner) : inner_(inner) { }
        ~SlowLinkTransporter() { }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            _publish<DataType, scheme>(data, group, transport_cfg);
            inner_.publish<DataType, scheme>(data, group, transport_cfg);
        }

        template<typename DataType, int scheme = scheme<DataType>()>
        void publish(std::shared_ptr<DataType> data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            if(data)
            {
                _publish<DataType, scheme>(*data, group, transport_cfg);
                inner_.publish<DataType, scheme>(data, group, transport_cfg);
            }
        }
            
        private:
        template<typename DataType, int scheme>
        void _publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg)
        {
            std::vector<char> bytes(SerializerParserHelper<DataType, scheme>::serialize(data));
            std::cout << "SlowLinkTransporter: Publishing to group [" << group << "], using scheme [" << MarshallingScheme::as_string(scheme) << "]: " << goby::util::hex_encode(std::string(bytes.begin(), bytes.end())) << std::endl;
        }

        std::unique_ptr<InnerTransporter> own_inner_;
        InnerTransporter& inner_;
        
        };

    
}

#endif

