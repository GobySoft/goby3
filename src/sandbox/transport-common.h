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
    
    class SerializationSubscriptionBase
    {
    public:
        virtual std::string::const_iterator post(std::string::const_iterator b, std::string::const_iterator e) const = 0;
        virtual std::vector<char>::const_iterator post(std::vector<char>::const_iterator b, std::vector<char>::const_iterator e) const = 0;
        virtual const std::string& type_name() const = 0;
        virtual const std::string& group() const = 0;
        virtual int scheme() const = 0;
    };

    template<typename DataType, int scheme_id>
        class SerializationSubscription : public SerializationSubscriptionBase
    {
    public:
        typedef std::function<void (std::shared_ptr<DataType> data, const std::string& group, const goby::protobuf::TransporterConfig& transport_cfg)> HandlerType;

    SerializationSubscription(HandlerType& handler,
                              const std::string& group)
        : handler_(handler),
            type_name_(SerializerParserHelper<DataType, scheme_id>::type_name(DataType())),
            group_(group)
            { }
            
        // handle an incoming message
        std::string::const_iterator post(std::string::const_iterator b, std::string::const_iterator e) const override
        { return _post(b, e); }
        
        std::vector<char>::const_iterator post(std::vector<char>::const_iterator b, std::vector<char>::const_iterator e) const override
        { return _post(b, e); }
            

        // getters
        const std::string& type_name() const { return type_name_; }
        const std::string& group() const { return group_; }            
        int scheme() const { return scheme_id; }

    private:
        template<typename CharIterator>
            CharIterator _post(CharIterator bytes_begin,
                               CharIterator bytes_end) const 
        {
            CharIterator actual_end;
            auto msg = std::make_shared<DataType>(SerializerParserHelper<DataType, scheme_id>::parse(bytes_begin, bytes_end, actual_end));
            handler_(msg, group_, goby::protobuf::TransporterConfig());
            return actual_end;
        }

    private:
        HandlerType handler_;
        const std::string type_name_;
        const std::string group_;
    };
    
    
    template<typename InnerTransporter, typename Group>
        class SerializationTransporterBase
    {
    public:
    SerializationTransporterBase(InnerTransporter& inner) : inner_(inner) { }
        ~SerializationTransporterBase() { }

        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(const DataType& data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                _publish<DataType, scheme>(data, group, transport_cfg);
                inner_.publish<DataType, scheme>(data, group_convert(group), transport_cfg);
            }

        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(std::shared_ptr<DataType> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                if(data)
                {
                    _publish<DataType, scheme>(*data, group, transport_cfg);
                    inner_.publish<DataType, scheme>(data, group_convert(group), transport_cfg);
                }
            }
        
        template<typename DataType, int scheme = scheme<DataType>()>
            void subscribe(const Group& group, std::function<void(const DataType&)> func)
            {
                // subscribe to inner transporter
                inner_.subscribe<DataType, scheme>(group_convert(group), func);

                // forward subscription to edge
                auto inner_publication_lambda = [=](std::shared_ptr<DataType> d, const std::string& g, const goby::protobuf::TransporterConfig& t) { inner_.template publish<DataType, scheme>(d, g, t); };
                typename SerializationSubscription<DataType, scheme>::HandlerType inner_publication_function(inner_publication_lambda);
                auto subscription = std::shared_ptr<SerializationSubscriptionBase>(new SerializationSubscription<DataType, scheme>(inner_publication_function, group_convert(group)));
                inner_.publish<SerializationSubscriptionBase, MarshallingScheme::CXX_OBJECT>(subscription, forward_group_name());
                std::cout << "Published subscription: " << subscription->type_name() << ":" << subscription->group() << std::endl;
            }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
            void subscribe(const Group& group, void(C::*mem_func)(const DataType&), C* c)
            {
                subscribe<DataType, scheme>(group, [=](const DataType& d) { (c->*mem_func)(d); });
            }
        
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return inner_.poll(timeout);
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            return poll(std::chrono::system_clock::now() + wait_for);
        }

    protected:
        virtual const std::string& forward_group_name() = 0;        
        
    private:
        template<typename DataType, int scheme>
            void _publish(const DataType& d, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            // create and forward publication to edge
            std::vector<char> bytes(SerializerParserHelper<DataType, scheme>::serialize(d));
            std::string* sbytes = new std::string(bytes.begin(), bytes.end());
            std::shared_ptr<goby::protobuf::SerializerTransporterData> data = std::make_shared<goby::protobuf::SerializerTransporterData>();

            data->set_marshalling_scheme(scheme);
            data->set_type(SerializerParserHelper<DataType, scheme>::type_name(d));
            data->set_group(group_convert(group));
            data->set_allocated_data(sbytes);
        
            *data->mutable_cfg() = transport_cfg;

            inner_.publish(data, forward_group_name());
        }

        InnerTransporter& inner_;
        
    };
}

#endif

