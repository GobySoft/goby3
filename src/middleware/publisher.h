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

#include "goby/acomms/protobuf/modem_message.pb.h"
#include "goby/middleware/group.h"
#include "goby/middleware/protobuf/transporter_config.pb.h"

namespace goby
{
namespace middleware
{
template <typename Data> class Publisher
{
  public:
    using set_group_func_type = std::function<void(Data&, const Group&)>;
    using acked_func_type =
        std::function<void(const Data&, const intervehicle::protobuf::AckData&)>;
    using expired_func_type =
        std::function<void(const Data&, const intervehicle::protobuf::ExpireData&)>;

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

    Publisher(const goby::middleware::protobuf::TransporterConfig& cfg, acked_func_type acked_func,
              expired_func_type expired_func = expired_func_type())
        : Publisher(cfg, set_group_func_type(), acked_func, expired_func)
    {
    }

    ~Publisher() {}

    const goby::middleware::protobuf::TransporterConfig& cfg() const { return cfg_; }

    void set_group(Data& data, const Group& group) const
    {
        if (set_group_func_)
            set_group_func_(data, group);
    };

    acked_func_type acked_func() const { return acked_func_; }
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
