#include "transport-interfaces.h"


int goby::PollerInterface::poll(const std::chrono::system_clock::time_point& timeout,
                                std::unique_lock<std::timed_mutex>* lock)
{
    return _poll_all(timeout, lock);
}

int goby::PollerInterface::poll(std::chrono::system_clock::duration wait_for,
                                std::unique_lock<std::timed_mutex>* lock)
{
    if(wait_for == std::chrono::system_clock::duration::max())
        return poll(std::chrono::system_clock::time_point::max(), lock);
    else
        return poll(std::chrono::system_clock::now() + wait_for, lock);
}

int goby::PollerInterface::_poll_all(const std::chrono::system_clock::time_point& timeout,
                                     std::unique_lock<std::timed_mutex>* lock)
{    
    
    std::unique_ptr<std::unique_lock<std::timed_mutex>> our_lock;
    if(lock == nullptr)
    {
        our_lock.reset(new std::unique_lock<std::timed_mutex>(*poll_mutex_));
        lock = our_lock.get();
    }
    
    int poll_items = _transporter_poll();
    if(poll_items == 0)
    {
        if(timeout == std::chrono::system_clock::time_point::max())
        {
            cv_->wait(*lock); // wait_until doesn't work well with time_point::max()
            poll_items = _transporter_poll();
            
            if(poll_items == 0)
                goby::glog.is(goby::common::logger::DEBUG1) && goby::glog << "PollerInterface condition_variable: spurious wakeup" << std::endl;            
            
        }
        else
        {
            if(cv_->wait_until(*lock, timeout) == std::cv_status::no_timeout)
                poll_items = _transporter_poll();
            else
                return poll_items;
        }
    }
    
    return poll_items;
}

