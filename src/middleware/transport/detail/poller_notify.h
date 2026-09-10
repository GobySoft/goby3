// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_DETAIL_POLLER_NOTIFY_H
#define GOBY_MIDDLEWARE_TRANSPORT_DETAIL_POLLER_NOTIFY_H

#include <condition_variable>
#include <mutex>

namespace goby
{
namespace middleware
{
namespace detail
{
/// \brief Wake a Poller's poll() loop after data has been made available to one of its _poll() implementations.
///
/// PollerInterface::_poll_all() holds poll_mutex from the poll that returned no items until
/// cv.wait() atomically releases it. Notifying without first taking poll_mutex can therefore signal
/// into that window, where there is no waiter yet and the wakeup is lost; with no timeout on the
/// wait, the data then sits unread until some other event notifies.
inline void notify_poller(std::mutex& poll_mutex, std::condition_variable& cv)
{
    {
        std::lock_guard<std::mutex> lock(poll_mutex);
    }
    cv.notify_all();
}

} // namespace detail
} // namespace middleware
} // namespace goby

#endif
