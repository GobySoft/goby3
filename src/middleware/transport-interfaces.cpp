#include "transport-interfaces.h"


int goby::PollerInterface::poll(const std::chrono::system_clock::time_point& timeout)
{
    return _poll_all(timeout);
}

int goby::PollerInterface::poll(std::chrono::system_clock::duration wait_for)
{
    if(wait_for == std::chrono::system_clock::duration::max())
        return poll();
    else
        return poll(std::chrono::system_clock::now() + wait_for);
}

int goby::PollerInterface::_poll_all(const std::chrono::system_clock::time_point& timeout)
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
                goby::glog.is(goby::common::logger::DEBUG1) && goby::glog << "PollerInterface condition_variable: spurious wakeup" << std::endl;            
                    
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

