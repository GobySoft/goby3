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

#ifndef DriverThread20190619H
#define DriverThread20190619H

#include "goby/acomms/amac.h"
#include "goby/acomms/buffer/dynamic_buffer.h"

#include "goby/middleware/group.h"
#include "goby/middleware/protobuf/intervehicle_config.pb.h"
#include "goby/middleware/thread.h"
#include "goby/middleware/transport-interthread.h"

namespace goby
{
namespace acomms
{
class ModemDriverBase;
} // namespace acomms

namespace middleware
{
namespace intervehicle
{
namespace groups
{
constexpr Group modem_data_out{"goby::middleware::intervehicle::modem_data_out"};
constexpr Group modem_data_in{"goby::middleware::intervehicle::modem_data_in"};
constexpr Group modem_driver_ready{"goby::middleware::intervehicle::modem_driver_ready"};
} // namespace groups

class ModemDriverThread
    : public goby::middleware::Thread<protobuf::InterVehiclePortalConfig::LinkConfig,
                                      InterThreadTransporter>
{
  public:
    ModemDriverThread(const protobuf::InterVehiclePortalConfig::LinkConfig& cfg);
    void loop() override;
    int tx_queue_size() { return sending_.size(); }

  private:
    void _data_request(goby::acomms::protobuf::ModemTransmission* msg);
    void _buffer_message(std::shared_ptr<const protobuf::SerializerTransporterMessage> msg);

    goby::acomms::DynamicBuffer<std::string>::subbuffer_id_type
    _create_buffer_id(const protobuf::SerializerTransporterKey& key);

  private:
    std::unique_ptr<InterThreadTransporter> interthread_;

    std::map<goby::acomms::DynamicBuffer<std::string>::subbuffer_id_type,
             std::shared_ptr<const protobuf::SerializerTransporterMessage> >
        publisher_buffer_cfg_;
    goby::acomms::DynamicBuffer<std::string> buffer_;

    std::deque<std::string> sending_;

    std::unique_ptr<goby::acomms::ModemDriverBase> driver_;
    goby::acomms::MACManager mac_;
};

} // namespace intervehicle
} // namespace middleware
} // namespace goby

#endif
