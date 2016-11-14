#ifndef TransportCommon20160607H
#define TransportCommon20160607H

#include <memory>
#include <unordered_map>
#include <chrono>

#include "goby/util/binary.h"

#include "serialize_parse.h"

#include "goby/sandbox/protobuf/transporter_config.pb.h"
#include "goby/sandbox/protobuf/interprocess_data.pb.h"

namespace goby
{    
    class NoOpTransporter
    {
    public:
        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void publish(const Data& data,
                         const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void publish(std::shared_ptr<Data> data,
                         const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(const Data&)> func)
            { }

        template<const Group& group, typename Data, int scheme = scheme<Data>()>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> func)
            { }

        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        { return 0; }
        
        int poll(std::chrono::system_clock::duration wait_for)
        { return 0; }
        
    };
    
    class SerializationSubscriptionBase
    {
    public:
        virtual std::string::const_iterator post(std::string::const_iterator b, std::string::const_iterator e) const = 0;
        virtual std::vector<char>::const_iterator post(std::vector<char>::const_iterator b, std::vector<char>::const_iterator e) const = 0;
        virtual const char* post(const char* b, const char* e) const = 0;
        virtual const std::string& type_name() const = 0;
        virtual const Group& subscribed_group() const = 0;

        virtual int scheme() const = 0;
    };

    template<typename Data, int scheme_id>
        class SerializationSubscription : public SerializationSubscriptionBase
    {
    public:
        typedef std::function<void (std::shared_ptr<Data> data, const goby::protobuf::TransporterConfig& transport_cfg)> HandlerType;

    SerializationSubscription(HandlerType& handler,
                              const Group& group,
                              std::function<Group(const Data&)> group_func)
        : handler_(handler),
            type_name_(SerializerParserHelper<Data, scheme_id>::type_name(Data())),
            group_(group),
            group_func_(group_func)
            { }
            
        
        // handle an incoming message
        std::string::const_iterator post(std::string::const_iterator b, std::string::const_iterator e) const override
        { return _post(b, e); }
        
        std::vector<char>::const_iterator post(std::vector<char>::const_iterator b, std::vector<char>::const_iterator e) const override
        { return _post(b, e); }

        const char* post(const char* b, const char* e) const override
        { return _post(b, e); }

        
        // getters
        const std::string& type_name() const override { return type_name_; }
        const Group& subscribed_group() const override { return group_; }
        int scheme() const override { return scheme_id; }

    private:
        template<typename CharIterator>
            CharIterator _post(CharIterator bytes_begin,
                               CharIterator bytes_end) const 
        {
            CharIterator actual_end;
            auto msg = std::make_shared<Data>(SerializerParserHelper<Data, scheme_id>::parse(bytes_begin, bytes_end, actual_end));
            if(subscribed_group() == group_func_(*msg))
                handler_(msg, goby::protobuf::TransporterConfig());
            return actual_end;
        }

        
    private:
        HandlerType handler_;
        const std::string type_name_;
        const Group group_;
        std::function<Group(const Data&)> group_func_;
    };

}
 
#endif

