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
