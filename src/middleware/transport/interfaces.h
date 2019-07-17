// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef TransportInterfaces20170808H
#define TransportInterfaces20170808H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "goby/middleware/group.h"
#include "goby/middleware/marshalling/interface.h"

#include "goby/exception.h"
#include "goby/middleware/protobuf/intervehicle.pb.h"
#include "goby/middleware/protobuf/transporter_config.pb.h"
#include "goby/middleware/publisher.h"
#include "goby/middleware/subscriber.h"
#include "goby/util/debug_logger.h"

namespace goby
{
namespace middleware
{
class NullTransporter;


template <typename Transporter, typename InnerTransporter> class StaticTransporterInterface
{
  public:
    template <const Group& group, typename Data,
              int scheme = transporter_scheme<Data, Transporter>()>
    void publish(const Data& data, const Publisher<Data>& publisher = Publisher<Data>())
    {
        static_cast<Transporter*>(this)->template check_validity<group>();
        static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group,
                                                                                publisher);
    }

    // need both const and non-const shared_ptr overload to ensure that the const& overload isn't preferred to these.
    template <const Group& group, typename Data,
              int scheme = transporter_scheme<Data, Transporter>()>
    void publish(std::shared_ptr<const Data> data,
                 const Publisher<Data>& publisher = Publisher<Data>())
    {
        static_cast<Transporter*>(this)->template check_validity<group>();
        static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group,
                                                                                publisher);
    }

    template <const Group& group, typename Data,
              int scheme = transporter_scheme<Data, Transporter>()>
    void publish(std::shared_ptr<Data> data, const Publisher<Data>& publisher = Publisher<Data>())
    {
        publish<group, Data, scheme>(std::shared_ptr<const Data>(data), publisher);
    }

    template <const Group& group, typename Data,
              int scheme = transporter_scheme<Data, Transporter>()>
    void subscribe(std::function<void(const Data&)> f,
                   const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_cast<Transporter*>(this)->template check_validity<group>();
        static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group,
                                                                                  subscriber);
    }
    template <const Group& group, typename Data,
              int scheme = transporter_scheme<Data, Transporter>()>
    void subscribe(std::function<void(std::shared_ptr<const Data>)> f,
                   const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_cast<Transporter*>(this)->template check_validity<group>();
        static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group,
                                                                                  subscriber);
    }

    template <const Group& group, typename Data,
              int scheme = transporter_scheme<Data, Transporter>()>
    void unsubscribe()
    {
        static_cast<Transporter*>(this)->template check_validity<group>();
        static_cast<Transporter*>(this)->template unsubscribe_dynamic<Data, scheme>(group);
    }

    void unsubscribe_all() { static_cast<Transporter*>(this)->template unsubscribe_all(); }

    using InnerTransporterType = InnerTransporter;
    InnerTransporter& inner()
    {
        static_assert(!std::is_same<InnerTransporter, NullTransporter>(),
                      "This transporter has no inner() transporter layer");
        return static_cast<Transporter*>(this)->inner_;
    }
};

class PollerInterface
{
  public:
    PollerInterface(std::shared_ptr<std::timed_mutex> poll_mutex,
                    std::shared_ptr<std::condition_variable_any> cv)
        : poll_mutex_(poll_mutex), cv_(cv)
    {
    }

    template <class Clock = std::chrono::system_clock, class Duration = typename Clock::duration>
    int poll(const std::chrono::time_point<Clock, Duration>& timeout =
                 std::chrono::time_point<Clock, Duration>::max());

    template <class Clock = std::chrono::system_clock, class Duration = typename Clock::duration>
    int poll(Duration wait_for);

    std::shared_ptr<std::timed_mutex> poll_mutex() { return poll_mutex_; }
    std::shared_ptr<std::condition_variable_any> cv() { return cv_; }

  private:
    template <typename Transporter> friend class Poller;
    // poll the transporter for data
    virtual int _transporter_poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock) = 0;

  private:
    // poll all the transporters for data, including a timeout (only called by the outside-most Poller)
    template <class Clock = std::chrono::system_clock, class Duration = typename Clock::duration>
    int _poll_all(const std::chrono::time_point<Clock, Duration>& timeout);

    std::shared_ptr<std::timed_mutex> poll_mutex_;
    // signaled when there's no data for this thread to read during _poll()
    std::shared_ptr<std::condition_variable_any> cv_;
};
} // namespace middleware
} // namespace goby

template <class Clock, class Duration>
int goby::middleware::PollerInterface::poll(const std::chrono::time_point<Clock, Duration>& timeout)
{
    return _poll_all(timeout);
}

template <class Clock, class Duration>
int goby::middleware::PollerInterface::poll(Duration wait_for)
{
    if (wait_for == Duration::max())
        return poll();
    else
        return poll(Clock::now() + wait_for);
}

template <class Clock, class Duration>
int goby::middleware::PollerInterface::_poll_all(
    const std::chrono::time_point<Clock, Duration>& timeout)
{
    // hold this lock until either we find a polled item or we wait on the condition variable
    std::unique_ptr<std::unique_lock<std::timed_mutex> > lock(
        new std::unique_lock<std::timed_mutex>(*poll_mutex_));
    //    std::cout << std::this_thread::get_id() <<  " _poll_all locking: " << poll_mutex_.get() << std::endl;

    int poll_items = _transporter_poll(lock);
    while (poll_items == 0)
    {
        if (!lock)
            throw(goby::Exception(
                "Poller lock was released by poll() but no poll items were returned"));

        if (timeout == Clock::time_point::max())
        {
            cv_->wait(*lock); // wait_until doesn't work well with time_point::max()
            poll_items = _transporter_poll(lock);

            if (poll_items == 0)
                goby::glog.is(goby::util::logger::DEBUG3) &&
                    goby::glog << "PollerInterface condition_variable: spurious wakeup"
                               << std::endl;
        }
        else
        {
            if (cv_->wait_until(*lock, timeout) == std::cv_status::no_timeout)
                poll_items = _transporter_poll(lock);
            else
                return poll_items;
        }
    }

    return poll_items;
}

#endif
