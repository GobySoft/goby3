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

#ifndef Publisher20190627H
#define Publisher20190627H

#include <functional>

#include "goby/acomms/protobuf/modem_message.pb.h"
#include "goby/middleware/group.h"
#include "goby/middleware/protobuf/transporter_config.pb.h"

namespace goby
{
namespace middleware
{
/// \brief Class that holds additional metadata and callback functions related to a publication (and is optionally provided as a parameter to StaticTransporterInterface::publish). Use of this class is generally unnecessary on interprocess and inner layers.
template <typename Data> class Publisher
{
  public:
    using set_group_func_type = std::function<void(Data&, const Group&)>;
    using acked_func_type =
        std::function<void(const Data&, const intervehicle::protobuf::AckData&)>;
    using expired_func_type =
        std::function<void(const Data&, const intervehicle::protobuf::ExpireData&)>;

    /// \brief Construct a Publisher with all available metadata and callbacks
    ///
    /// \param cfg Additional metadata for all publish calls for which this Publisher is provided
    /// \param set_group_func Callback function for setting the group for a given data type if not provided in the parameters to the publish call. This is typically used when the group is defined or inferred from data in the message itself, and thus using this callback avoids duplicated data on the slow links used in the intervehicle and outer layers by setting the group value in the message contents itself (as opposed to transmitted in the header).
    /// \param acked_func Callback function for when data is acknowledged by subscribers to this publication
    /// \param expired_func Callback function for when data expires without reaching any subscribers (either because none exist or because the link(s) failed to transfer the data within the time to live).
    Publisher(const goby::middleware::protobuf::TransporterConfig& cfg =
                  goby::middleware::protobuf::TransporterConfig(),
              set_group_func_type set_group_func = set_group_func_type(),
              acked_func_type acked_func = acked_func_type(),
              expired_func_type expired_func = expired_func_type())
        : cfg_(cfg),
          set_group_func_(set_group_func),
          acked_func_(acked_func),
          expired_func_(expired_func)
    {
        // if an ack function is set, we assume we require an ack
        if (acked_func_ && !cfg_.intervehicle().buffer().has_ack_required())
        {
            cfg_.mutable_intervehicle()->mutable_buffer()->set_ack_required(true);
        }
    }

    /// \brief Construct a Publisher but without the set_group_func callback
    Publisher(const goby::middleware::protobuf::TransporterConfig& cfg, acked_func_type acked_func,
              expired_func_type expired_func = expired_func_type())
        : Publisher(cfg, set_group_func_type(), acked_func, expired_func)
    {
    }

    ~Publisher() {}

    /// \brief Returns the metadata configuration
    const goby::middleware::protobuf::TransporterConfig& cfg() const { return cfg_; }

    /// \brief Sets the group using the set_group_func. Only intended to be called by the various transporters.
    void set_group(Data& data, const Group& group) const
    {
        if (set_group_func_)
            set_group_func_(data, group);
    };

    /// \brief Returns the acked data callback (or an empty function if none is set)
    acked_func_type acked_func() const { return acked_func_; }
    /// \brief Returns the expired data callback  (or an empty function if none is set)
    expired_func_type expired_func() const { return expired_func_; }

  private:
    goby::middleware::protobuf::TransporterConfig cfg_;
    set_group_func_type set_group_func_;
    acked_func_type acked_func_;
    expired_func_type expired_func_;
};

} // namespace middleware
} // namespace goby

#endif
