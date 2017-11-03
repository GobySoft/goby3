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
        
        
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            return _poll_all(timeout);
        }
        
        int poll(std::chrono::system_clock::duration wait_for)
        {
            if(wait_for == std::chrono::system_clock::duration::max())
                return poll();
            else
                return poll(std::chrono::system_clock::now() + wait_for);
        }

        std::shared_ptr<std::timed_mutex> poll_mutex() { return poll_mutex_; }
        std::shared_ptr<std::condition_variable_any> cv() { return cv_; }
        
    private:
        template<typename Transporter> friend class Poller;
        // poll the transporter for data
        virtual int _transporter_poll() = 0;

    private:
        // poll all the transporters for data, including a timeout (only called by the outside-most Poller)
        int _poll_all(const std::chrono::system_clock::time_point& timeout)
        {
            std::unique_lock<std::timed_mutex> lock(*poll_mutex_);
            int poll_items = _transporter_poll();
            
            while(poll_items == 0) // no items, so wait
            {                
                if(timeout == std::chrono::system_clock::time_point::max())
                {
                    cv_->wait(lock); // wait_until doesn't work well with time_point::max()
                    poll_items = _transporter_poll();

                    if(poll_items == 0)
                        goby::glog.is(goby::common::logger::DEBUG1) && goby::glog << "Spurious wakeup" << std::endl;            
                    
                }
                else
                {
                    if(cv_->wait_until(lock, timeout) == std::cv_status::no_timeout)
                        poll_items = _transporter_poll();
                    else
                        return poll_items;
                }
            }

            return poll_items;
        }
        
        std::shared_ptr<std::timed_mutex> poll_mutex_;
        // signaled when there's no data for this thread to read during _poll()
        std::shared_ptr<std::condition_variable_any> cv_;
    };

    template<typename Transporter>
        class Poller : public PollerInterface
    {
    protected:
        Poller(PollerInterface* inner_poller = nullptr) :
        // we want the same mutex and cv all the way up
        PollerInterface(inner_poller ? inner_poller->poll_mutex() : std::make_shared<std::timed_mutex>(),
                        inner_poller ? inner_poller->cv() : std::make_shared<std::condition_variable_any>()),
            inner_poller_(inner_poller)
        { }

        PollerInterface* inner_poller() { return inner_poller_; }
        
    private:
        int _transporter_poll()
        {
            // work from the inside out
            int inner_poll_items = 0;
            if(inner_poller_) // recursively call inner poll
                inner_poll_items += static_cast<PollerInterface*>(inner_poller_)->_transporter_poll();    

            int poll_items = 0;
            if(!inner_poll_items)
                poll_items += static_cast<Transporter*>(this)->template _poll();

            goby::glog.is(goby::common::logger::DEBUG3) && goby::glog << "Poller::transporter_poll(): " << typeid(*this).name() << " this: " << this << " (" << poll_items << " items) "<< " inner_poller_: " << inner_poller_ << " (" << inner_poll_items << " items) " << std::endl;            

            return inner_poll_items + poll_items;
        }

    private:
        PollerInterface* inner_poller_;

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
