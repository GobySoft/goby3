// Copyright 2019-2020:
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_NULL_H
#define GOBY_MIDDLEWARE_TRANSPORT_NULL_H

#include "goby/middleware/transport/poller.h"

#include "interface.h"

namespace goby
{
namespace middleware
{
/// \brief A do-nothing transporter that is always inside the last real transporter level. You will never directly instantiate a NullTransporter if you want it to do anything.
class NullTransporter : public StaticTransporterInterface<NullTransporter, NullTransporter>,
                        public Poller<NullTransporter>
{
  public:
    NullTransporter() = default;
    virtual ~NullTransporter() = default;

    template <typename Data> static constexpr int scheme()
    {
        return MarshallingScheme::NULL_SCHEME;
    }

    template <const Group& group> void check_validity() {}

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void unsubscribe_dynamic(const Group& group)
    {
    }

  private:
    friend Poller<NullTransporter>;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock) { return 0; }
};
} // namespace middleware
} // namespace goby

#endif
