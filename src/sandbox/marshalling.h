#ifndef Marshalling20160607H
#define Marshalling20160607H

#include <memory>

#include "transport.h"
#include "serialize_parse.h"

namespace goby
{
    
    class ProtobufMarshaller
    {
    public:
        template<typename ProtobufMessage, typename Transporter>
            void publish(const ProtobufMessage& msg,
                         const std::string& group,
                         Transporter& transporter,
                         const TransporterConfig& transport_cfg = TransporterConfig())
        {
            const std::vector<char> bytes(SP<ProtobufMessage, MarshallingScheme::PROTOBUF>::serialize(msg));
            transporter.publish<MarshallingScheme::PROTOBUF>(bytes.begin(), bytes.end(), group, transport_cfg);
        }
        

        template<typename ProtobufMessage, typename Transporter>
            void publish(std::shared_ptr<ProtobufMessage> msg,
                         const std::string group,
                         Transporter& transporter,
                         const TransporterConfig& transport_cfg = TransporterConfig())
        {
            transporter.publish<MarshallingScheme::PROTOBUF>(msg, group, transport_cfg);
        }
        

        
        
        
    };
}



#endif
