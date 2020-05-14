// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Ryan Govostes <rgovostes+git@gmail.com>
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

#include "interface.h"

namespace goby
{
namespace middleware
{
/// \brief Utility class for allowing the various Goby middleware transporters to poll the underlying transport code for data
///
/// This class is recursively instantiated with each inner poller passed as a parameter to the next outer poller. This allows the outermost Poller to poll all inner Poller instantiations as well as itself.
template <typename Transporter> class Poller : public PollerInterface
{
  protected:
    /// Construct this Poller with a pointer to the inner Poller (unless this is the innermost Poller)
    Poller(PollerInterface* inner_poller = nullptr)
        : // we want the same mutex and cv all the way up
          PollerInterface(
              inner_poller ? inner_poller->poll_mutex() : std::make_shared<std::timed_mutex>(),
              inner_poller ? inner_poller->cv() : std::make_shared<std::condition_variable_any>()),
          inner_poller_(inner_poller)
    {
    }

    /// \return Pointer to the inner Poller
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
