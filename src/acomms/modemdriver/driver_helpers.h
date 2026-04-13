// Copyright 2024-2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Jared Silbermann <jared.silbermann@missionsystems.com.au>
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

// Converts the dccl binary to the required comma seperated bytes -- duplication from popoto driver (will fix)
#include "goby/acomms/modemdriver/driver_base.h"   // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"   // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmi...
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmission
#include "goby/exception.h"                        // for Exception
#include "goby/util/thirdparty/nlohmann/json.hpp"  // for json
#include <string>
using json = nlohmann::json;

namespace goby
{
namespace acomms
{

// Used by Mission Systems Popoto and Janus drivers
enum GobyHeaderBits
{
    GOBY_DATA_TYPE = 1,
    GOBY_ACK_TYPE = 2,
    GOBY_HEADER_TYPE = 0,       // 0 == Data, 1 == Ack
    GOBY_HEADER_ACK_REQUEST = 1 // 0 == no ack requested, 1 == ack requested
};

}
} // namespace goby
