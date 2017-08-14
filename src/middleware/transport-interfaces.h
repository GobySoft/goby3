#ifndef TransportInterfaces20170808H
#define TransportInterfaces20170808H

#include <memory>

#include "serialize_parse.h"
#include "group.h"

#include "goby/middleware/protobuf/transporter_config.pb.h"

namespace goby
{
    class NullTransporter;
    
    template<typename Transporter, typename InnerTransporter>
        class StaticTransporterInterface
    {
    public:
        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void publish(const Data& data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void publish(std::shared_ptr<Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(const Data&)> f)
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group);
            }
        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> f)
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group);
            }
        
        InnerTransporter& inner()
        {
            static_assert(!std::is_same<InnerTransporter, NullTransporter>(), "This transporter has no inner() transporter layer");
            return static_cast<Transporter*>(this)->inner_;
        }
    };
    
    template<typename Transporter>
        class PollAbsoluteTimeInterface
    {
    public:
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return static_cast<Transporter*>(this)->template _poll(timeout);
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            if(wait_for == std::chrono::system_clock::duration::max())
                return poll();
            else
                return poll(std::chrono::system_clock::now() + wait_for);
        }
    };

    template<typename Transporter>
        class PollRelativeTimeInterface
    {
    public:
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            if(timeout == std::chrono::system_clock::time_point::max())
                return poll(std::chrono::system_clock::duration::max());
            else
                return poll(timeout - std::chrono::system_clock::now());
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            return static_cast<Transporter*>(this)->template _poll(wait_for);
        }

    };    
}


#endif
