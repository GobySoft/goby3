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
        template<MarshallingScheme scheme, typename CharIterator>
            void publish(CharIterator data_begin, CharIterator data_end,
                         const std::string& group, const TransporterConfig& transport_cfg)
        {
            std::cout << "NoOp publish" << std::endl;
        }

        template<MarshallingScheme scheme, typename DataType>
            void publish(std::shared_ptr<DataType> data,
                         const std::string& group, const TransporterConfig& transport_cfg) { }
    };
    
    
    template<typename InnerTransporter = NoOpTransporter>
        class ZMQTransporter : public InnerTransporter
        {
        public:
        template<MarshallingScheme scheme, typename CharIterator>
        void publish(CharIterator data_begin, CharIterator data_end,
                     const std::string& group, const TransporterConfig& transport_cfg)
        {
            _publish(data_begin, data_end, group, transport_cfg);
            InnerTransporter::template publish<scheme, CharIterator>(data_begin, data_end, group, transport_cfg);
        }

        template<MarshallingScheme scheme, typename DataType>
        void publish(std::shared_ptr<DataType> data, const std::string& group, const TransporterConfig& transport_cfg)
        {
            if(data)
            {
                std::vector<char> bytes(SP<DataType, scheme>::serialize(*data));
                _publish(bytes.begin(), bytes.end(), group, transport_cfg);
                InnerTransporter::template publish<scheme, DataType>(data, group, transport_cfg);

            }
        }
            
        private:
        template<typename CharIterator>
        void _publish(CharIterator data_begin, CharIterator data_end,
                     const std::string& group, const TransporterConfig& transport_cfg)
        {
            std::cout << "Publishing: " << goby::util::hex_encode(std::string(data_begin, data_end)) << std::endl;
        }
        
        
        };

    class IntraProcessTransporter 
    {
    public:
        template<MarshallingScheme scheme, typename CharIterator>
            void publish(CharIterator data_begin, CharIterator data_end, const std::string& group, const TransporterConfig& transport_cfg)
        {
            static_assert(False<CharIterator>(), "ZMQTransporter does not implement serialized form of publish(). You must use a std::shared_ptr<>()");
        }

        template<MarshallingScheme scheme, typename DataType>
            void publish(std::shared_ptr<DataType> data, const std::string& group, const TransporterConfig& transport_cfg)
        {
        }
    };
    
    
}

#endif

