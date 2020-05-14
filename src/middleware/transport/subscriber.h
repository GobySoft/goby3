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

#ifndef Subscriber20190627H
#define Subscriber20190627H

#include "goby/middleware/group.h"
#include "goby/middleware/protobuf/transporter_config.pb.h"
#include "goby/middleware/transport/publisher.h"

namespace goby
{
namespace middleware
{
/// \brief Class that holds additional metadata and callback functions related to a subscription (and is optionally provided as a parameter to StaticTransporterInterface::subscribe). Use of this class is generally unnecessary on interprocess and inner layers.
template <typename Data> class Subscriber
{
  public:
    using group_func_type = std::function<Group(const Data&)>;
    using subscribed_func_type =
        typename Publisher<intervehicle::protobuf::Subscription>::acked_func_type;
    using subscribe_expired_func_type =
        typename Publisher<intervehicle::protobuf::Subscription>::expired_func_type;

    /// \brief Construct a Subscriber with all available metadata and callbacks
    ///
    /// \param cfg Additional metadata for this subscribe
    /// \param group_func Callback function for retrieving the group from a given data type if not provided in the parameters to the subscribe call. This is typically used when the group is defined or inferred from data in the message itself, and thus using this callback avoids duplicated data on the slow links used in the intervehicle and outer layers.
    /// \param subscribed_func Callback function for when a subscription request reaches a publisher for this data type
    /// \param subscribe_expired_func Callback function for when a subscription request expires without reaching any publishers (either because none exist or because the link(s) failed to transfer the request within the time to live).
    Subscriber(const goby::middleware::protobuf::TransporterConfig& cfg =
                   goby::middleware::protobuf::TransporterConfig(),
               group_func_type group_func = group_func_type(),
               subscribed_func_type subscribed_func = subscribed_func_type(),
               subscribe_expired_func_type subscribe_expired_func = subscribe_expired_func_type())
        : cfg_(cfg),
          group_func_(group_func),
          subscribed_func_(subscribed_func),
          subscribe_expired_func_(subscribe_expired_func)
    {
    }

    /// \brief Construct a Subscriber but without the group_func callback
    Subscriber(const goby::middleware::protobuf::TransporterConfig& cfg,
               subscribed_func_type subscribed_func,
               subscribe_expired_func_type subscribe_expired_func = subscribe_expired_func_type())
        : Subscriber(cfg, group_func_type(), subscribed_func, subscribe_expired_func)
    {
    }

    ~Subscriber() {}

    /// \return the metadata configuration
    const goby::middleware::protobuf::TransporterConfig& cfg() const { return cfg_; }

    /// \return the group for this subscribe call using the group_func. Only intended to be called by the various transporters.
    Group group(const Data& data) const
    {
        if (group_func_)
            return group_func_(data);
        else
            return Group(Group::broadcast_group);
    }

    /// \return the subscription successful callback (or an empty function if none is set)
    subscribed_func_type subscribed_func() const { return subscribed_func_; }
    /// \return the subscription request expired callback (or an empty function if none is set)
    subscribe_expired_func_type subscribe_expired_func() const { return subscribe_expired_func_; }

  private:
    goby::middleware::protobuf::TransporterConfig cfg_;
    group_func_type group_func_;
    subscribed_func_type subscribed_func_;
    subscribe_expired_func_type subscribe_expired_func_;
};
} // namespace middleware
} // namespace goby

#endif
