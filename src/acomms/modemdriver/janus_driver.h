// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_JANUS_DRIVER_H
#define GOBY_ACOMMS_MODEMDRIVER_JANUS_DRIVER_H

#include <cstdint> // for uint8_t
#include <map>      // for map
#include <string>   // for string
#include <vector>   // for vector

#include "goby/acomms/modemdriver/driver_base.h"   // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"   // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmission
#include "goby/acomms/protobuf/janus_driver.pb.h" // for Config, MessageTy...
#include "goby/util/thirdparty/nlohmann/json.hpp"  // for json

namespace goby
{
namespace acomms
{
namespace protobuf
{
class ModemTransmission;
} // namespace protobuf

/// \brief provides an API to the imaginary ABC modem (as an example how to write drivers)
/// \ingroup acomms_api
///
class JanusDriver : public ModemDriverBase
{
  public:
    JanusDriver();
    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;
    int verbosity;
    std::string pset_file;
    int pset_id;
    int class_id;
    int application_type;
    std::uint32_t next_frame_{0};
    static constexpr int DEFAULT_MTU_BYTES{1024};

  private:
    const janus::protobuf::Config& janus_driver_cfg() const
    {
        return driver_cfg_.GetExtension(janus::protobuf::config);
    }

  private:
    enum
    {
        DEFAULT_BAUD = 4800
    };

    protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
    // rest is up to you!
};
} // namespace acomms
} // namespace goby
#endif
