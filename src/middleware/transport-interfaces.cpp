#include "transport-interfaces.h"
#include "goby/common/exception.h"
#include <thread>

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
    // hold this lock until either we find a polled item or we wait on the condition variable
    std::unique_ptr<std::unique_lock<std::timed_mutex>> lock(new std::unique_lock<std::timed_mutex>(*poll_mutex_));
//    std::cout << std::this_thread::get_id() <<  " _poll_all locking: " << poll_mutex_.get() << std::endl;
    
    int poll_items = _transporter_poll(lock);
    while(poll_items == 0)
    { 
	if(!lock)
	    throw(goby::Exception("Poller lock was released by poll() but no poll items were returned"));

        if(timeout == std::chrono::system_clock::time_point::max())
        {
            cv_->wait(*lock); // wait_until doesn't work well with time_point::max()
            poll_items = _transporter_poll(lock);
            
            if(poll_items == 0)
                goby::glog.is(goby::common::logger::DEBUG1) && goby::glog << "PollerInterface condition_variable: spurious wakeup" << std::endl;            
            
        }
        else
        {
            if(cv_->wait_until(*lock, timeout) == std::cv_status::no_timeout)
                poll_items = _transporter_poll(lock);
            else
                return poll_items;
        }
    }
    
    return poll_items;
}

