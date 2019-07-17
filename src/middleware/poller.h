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

#ifndef Poller20171107H
#define Poller20171107H

#include "transport/interfaces.h"

namespace goby
{
namespace middleware
{
template <typename Transporter> class Poller : public PollerInterface
{
  protected:
    Poller(PollerInterface* inner_poller = nullptr)
        : // we want the same mutex and cv all the way up
          PollerInterface(
              inner_poller ? inner_poller->poll_mutex() : std::make_shared<std::timed_mutex>(),
              inner_poller ? inner_poller->cv() : std::make_shared<std::condition_variable_any>()),
          inner_poller_(inner_poller)
    {
    }

    PollerInterface* inner_poller() { return inner_poller_; }

  private:
    int _transporter_poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock) override
    {
        // work from the inside out
        int inner_poll_items = 0;
        if (inner_poller_) // recursively call inner poll
            inner_poll_items +=
                static_cast<PollerInterface*>(inner_poller_)->_transporter_poll(lock);

        int poll_items = 0;
        if (!inner_poll_items)
            poll_items += static_cast<Transporter*>(this)->_poll(lock);

        //            goby::glog.is(goby::util::logger::DEBUG3) && goby::glog << "Poller::transporter_poll(): " << typeid(*this).name() << " this: " << this << " (" << poll_items << " items) "<< " inner_poller_: " << inner_poller_ << " (" << inner_poll_items << " items) " << std::endl;

        return inner_poll_items + poll_items;
    }

  private:
    PollerInterface* inner_poller_;
};
} // namespace goby
} // namespace goby

#endif
