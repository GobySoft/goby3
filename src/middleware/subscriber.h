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

#ifndef Subscriber20190627H
#define Subscriber20190627H

#include "goby/middleware/group.h"
#include "goby/middleware/protobuf/transporter_config.pb.h"
#include "goby/middleware/publisher.h"

namespace goby
{
namespace middleware
{
template <typename Data> class Subscriber
{
  public:
    using group_func_type = std::function<Group(const Data&)>;
    using subscribed_func_type =
        typename Publisher<intervehicle::protobuf::Subscription>::acked_func_type;
    using subscribe_expired_func_type =
        typename Publisher<intervehicle::protobuf::Subscription>::expired_func_type;

    Subscriber(const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                   goby::middleware::protobuf::TransporterConfig(),
               group_func_type group_func = group_func_type(),
               subscribed_func_type subscribed_func = subscribed_func_type(),
               subscribe_expired_func_type subscribe_expired_func = subscribe_expired_func_type())
        : transport_cfg_(transport_cfg),
          group_func_(group_func),
          subscribed_func_(subscribed_func),
          subscribe_expired_func_(subscribe_expired_func)
    {
    }

    Subscriber(const goby::middleware::protobuf::TransporterConfig& transport_cfg,
               subscribed_func_type subscribed_func,
               subscribe_expired_func_type subscribe_expired_func = subscribe_expired_func_type())
        : Subscriber(transport_cfg, group_func_type(), subscribed_func, subscribe_expired_func)
    {
    }

    ~Subscriber() {}

    const goby::middleware::protobuf::TransporterConfig& transport_cfg() const
    {
        return transport_cfg_;
    }

    Group group(const Data& data) const
    {
        if (group_func_)
            return group_func_(data);
        else
            return Group(Group::broadcast_group);
    }

    subscribed_func_type subscribed_func() const { return subscribed_func_; }
    subscribe_expired_func_type subscribe_expired_func() const { return subscribe_expired_func_; }

  private:
    goby::middleware::protobuf::TransporterConfig transport_cfg_;
    group_func_type group_func_;
    subscribed_func_type subscribed_func_;
    subscribe_expired_func_type subscribe_expired_func_;
};
} // namespace middleware
} // namespace goby

#endif
