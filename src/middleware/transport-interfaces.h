#ifndef TransportInterfaces20170808H
#define TransportInterfaces20170808H

#include <memory>
#include <mutex>
#include <condition_variable>

#include "serialize_parse.h"
#include "group.h"

#include "goby/middleware/protobuf/transporter_config.pb.h"
#include "goby/common/logger.h"

namespace goby
{
    class NullTransporter;
    
    template<typename Transporter, typename InnerTransporter>
        class StaticTransporterInterface
    {
    public:
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void publish(const Data& data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }

        // need both const and non-const shared_ptr overload to ensure that the const& overload isn't preferred to these.
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void publish(std::shared_ptr<const Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }
        
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void publish(std::shared_ptr<Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                publish<group, Data, scheme>(std::shared_ptr<const Data>(data), transport_cfg);
            }

        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void subscribe(std::function<void(const Data&)> f)
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group);
            }
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
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


    class PollerInterface
    {
    public:
    PollerInterface(std::shared_ptr<std::timed_mutex> poll_mutex,
                    std::shared_ptr<std::condition_variable_any> cv) :
        poll_mutex_(poll_mutex),
            cv_(cv)
            { }
        
        
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max(), std::unique_lock<std::timed_mutex>* lock = nullptr);
        int poll(std::chrono::system_clock::duration wait_for, std::unique_lock<std::timed_mutex>* lock = nullptr);

        std::shared_ptr<std::timed_mutex> poll_mutex() { return poll_mutex_; }
        std::shared_ptr<std::condition_variable_any> cv() { return cv_; }
        
    private:
        template<typename Transporter> friend class Poller;
        // poll the transporter for data
        virtual int _transporter_poll() = 0;

    private:
        // poll all the transporters for data, including a timeout (only called by the outside-most Poller)
        int _poll_all(const std::chrono::system_clock::time_point& timeout,
                      std::unique_lock<std::timed_mutex>* lock = nullptr);
        
        std::shared_ptr<std::timed_mutex> poll_mutex_;
        // signaled when there's no data for this thread to read during _poll()
        std::shared_ptr<std::condition_variable_any> cv_;
    };
}


#endif
