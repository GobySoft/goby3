// Converts the dccl binary to the required comma seperated bytes -- duplication from popoto driver (will fix)
#include <string>
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmi...
#include "goby/acomms/modemdriver/driver_base.h"   // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"   // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmission
#include "goby/exception.h"                        // for Exception
#include "goby/util/thirdparty/nlohmann/json.hpp"  // for json
using json = nlohmann::json;

namespace goby
{
namespace acomms
{

enum GobyHeaderBits
{
    GOBY_HEADER_TYPE = 0,       // 0 == Data, 1 == Ack
    GOBY_HEADER_ACK_REQUEST = 1 // 0 == no ack requested, 1 == ack requested
};

static std::string binary_to_json(const std::uint8_t* buf, size_t num_bytes)
{
    std::string output;

    for (int i = 0, n = num_bytes; i < n; i++)
    {
        output.append(std::to_string((uint8_t)buf[i]));
        if (i < n - 1)
        {
            output.append(",");
        }
    }
    return output;
}

// Convert csv values back to dccl binary for the dccl codec to decode
static std::string json_to_binary(const json& element)
{
    std::string output;
    for (auto& subel : element) { output.append(1, (char)((uint8_t)subel)); }
    return output;
}

static std::uint16_t  CreateGobyHeader(const protobuf::ModemTransmission& m)
{
    std::uint16_t header{0};
    if (m.type() == protobuf::ModemTransmission::DATA)
    {
        header |= 0 << GOBY_HEADER_TYPE;
        header |= (m.ack_requested() ? 1 : 0) << GOBY_HEADER_ACK_REQUEST;
        header = header * 256;
        header |= m.frame_start();
    }
    else if (m.type() == protobuf::ModemTransmission::ACK)
    {
        header |= 1 << GOBY_HEADER_TYPE;
        header = header * 256;
        header |= m.frame_start();
    }
    else
    {
        throw(goby::Exception(std::string("Unsupported type provided to CreateGobyHeader: ") +
                              protobuf::ModemTransmission::TransmissionType_Name(m.type())));
    }
    return header;
}

static void DecodeGobyHeader(std::uint8_t header, std::uint8_t ack_num,
                                                  protobuf::ModemTransmission& m)
{
    m.set_type(( header & (1 << GOBY_HEADER_TYPE)) ? protobuf::ModemTransmission::ACK
                                                  : protobuf::ModemTransmission::DATA);
    if (m.type() == protobuf::ModemTransmission::DATA){
        m.set_ack_requested( header & (1 << GOBY_HEADER_ACK_REQUEST));
        m.set_frame_start( ack_num );
    }
}

// Remove popoto trash from the incoming serial string
static std::string StripString(std::string in, std::string p)
{
    std::string out = std::move(in);
    std::string::size_type n = p.length();
    for (std::string::size_type i = out.find(p); i != std::string::npos; i = out.find(p))
        out.erase(i, n);

    return out;
}

};
};