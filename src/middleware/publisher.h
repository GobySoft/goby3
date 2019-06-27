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
        std::function<void(const Data&, const goby::acomms::protobuf::ModemTransmission&)>;

    Publisher(const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                  goby::middleware::protobuf::TransporterConfig(),
              set_group_func_type set_group_func = set_group_func_type(),
              acked_func_type acked_func = acked_func_type())
        : transport_cfg_(transport_cfg), set_group_func_(set_group_func), acked_func_(acked_func)
    {
    }

    Publisher(const goby::middleware::protobuf::TransporterConfig& transport_cfg,
              acked_func_type acked_func)
        : Publisher(transport_cfg, set_group_func_type(), acked_func)
    {
    }

    ~Publisher() {}

    const goby::middleware::protobuf::TransporterConfig& transport_cfg() const
    {
        return transport_cfg_;
    }

    void set_group(Data& data, const Group& group) const
    {
        if (set_group_func_)
            set_group_func_(data, group);
    };

    void acked(const Data& original, const goby::acomms::protobuf::ModemTransmission& ack_msg) const
    {
        if (acked_func_)
            acked_func_(original, ack_msg);
    }

    acked_func_type acked_func() const { return acked_func_; }

  private:
    goby::middleware::protobuf::TransporterConfig transport_cfg_;
    set_group_func_type set_group_func_;
    acked_func_type acked_func_;
};

} // namespace middleware
} // namespace goby

#endif
