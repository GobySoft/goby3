#ifndef Poller20171107H
#define Poller20171107H

#include "transport-interfaces.h"

namespace goby
{
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

            //            goby::glog.is(goby::common::logger::DEBUG3) && goby::glog << "Poller::transporter_poll(): " << typeid(*this).name() << " this: " << this << " (" << poll_items << " items) "<< " inner_poller_: " << inner_poller_ << " (" << inner_poll_items << " items) " << std::endl;            

            return inner_poll_items + poll_items;
        }

    private:
        PollerInterface* inner_poller_;

    };
}

#endif
