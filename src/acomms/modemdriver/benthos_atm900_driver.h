// Copyright 2011-2023:
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_BENTHOS_ATM900_DRIVER_H
#define GOBY_ACOMMS_MODEMDRIVER_BENTHOS_ATM900_DRIVER_H

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF, is_...
#include <boost/algorithm/string/constants.hpp>      // for token_compress_on
#include <boost/algorithm/string/split.hpp>          // for split
#include <boost/any.hpp>                             // for bad_any_cast
#include <cstdint>                                   // for uint32_t
#include <dccl/bitset.h>                             // for Bitset
#include <dccl/codec.h>                              // for Codec
#include <dccl/common.h>                             // for uint32
#include <dccl/exception.h>                          // for NullValueException
#include <dccl/field_codec_fixed.h>                  // for TypedFixedField...
#include <dccl/field_codec_manager.h>                // for FieldCodecManager
#include <dccl/version.h>
#include <memory> // for shared_ptr, __s...
#include <string> // for string, operator+
#include <vector> // for vector

#include "benthos_atm900_driver_fsm.h"              // for BenthosATM900FSM
#include "driver_base.h"                            // for ModemDriverBase
#include "goby/acomms/protobuf/benthos_atm900.pb.h" // for BenthosHeader
#include "goby/acomms/protobuf/driver_base.pb.h"    // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h"  // for ModemTransmission
#include "goby/util/dccl_compat.h"
#include "iridium_rudics_packet.h" // for parse_rudics_pa...

namespace goby
{
namespace acomms
{
class BenthosATM900Driver : public ModemDriverBase
{
  public:
    BenthosATM900Driver();
    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;

  private:
    void receive(const protobuf::ModemTransmission& msg);
    void send(const protobuf::ModemTransmission& msg);
    void try_serial_tx();

    const benthos::protobuf::Config& benthos_driver_cfg() const
    {
        return driver_cfg_.GetExtension(benthos::protobuf::config);
    }

  private:
    enum
    {
        DEFAULT_BAUD = 9600
    };
    static const std::string SERIAL_DELIMITER;

    protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
    benthos::fsm::BenthosATM900FSM fsm_;
};

// placeholder id codec that uses no bits, since we're always sending just this message on the wire
class NoOpIdentifierCodec : public dccl::TypedFixedFieldCodec<dccl::uint32>
{
    dccl::Bitset encode() override { return dccl::Bitset(); }
    dccl::Bitset encode(const std::uint32_t& /*wire_value*/) override { return dccl::Bitset(); }
    dccl::uint32 decode(dccl::Bitset* /*bits*/) override { return 0; }
    unsigned size() override { return 0; }
};

extern std::shared_ptr<dccl::Codec> benthos_header_dccl_;

inline void init_benthos_dccl()
{
    auto benthos_id_name = "benthos_header_id";
#ifdef DCCL_VERSION_4_1_OR_NEWER
    benthos_header_dccl_.reset(new dccl::Codec(benthos_id_name, NoOpIdentifierCodec()));
#else
    dccl::FieldCodecManager::add<NoOpIdentifierCodec>(benthos_id_name);
    benthos_header_dccl_.reset(new dccl::Codec(benthos_id_name));
#endif

    benthos_header_dccl_->load<benthos::protobuf::BenthosHeader>();
}

inline void serialize_benthos_modem_message(std::string* out,
                                            const goby::acomms::protobuf::ModemTransmission& in)
{
    benthos::protobuf::BenthosHeader header;
    header.set_type(in.type());
    if (in.has_ack_requested())
        header.set_ack_requested(in.ack_requested());

    for (int i = 0, n = in.acked_frame_size(); i < n; ++i)
        header.add_acked_frame(in.acked_frame(i));

    benthos_header_dccl_->encode(out, header);

    // frame message
    for (int i = 0, n = in.frame_size(); i < n; ++i)
    {
        if (in.frame(i).empty())
            break;

        std::string rudics_packet;
        serialize_rudics_packet(in.frame(i), &rudics_packet, "\r", false);
        *out += rudics_packet;
    }
}

inline void parse_benthos_modem_message(std::string in,
                                        goby::acomms::protobuf::ModemTransmission* out)
{
    benthos::protobuf::BenthosHeader header;
    benthos_header_dccl_->decode(&in, &header);

    out->set_type(header.type());
    if (header.has_ack_requested())
        out->set_ack_requested(header.ack_requested());

    for (int i = 0, n = header.acked_frame_size(); i < n; ++i)
        out->add_acked_frame(header.acked_frame(i));

    std::vector<std::string> encoded_frames;
    boost::split(encoded_frames, in, boost::is_any_of("\r"), boost::token_compress_on);

    for (auto& encoded_frame : encoded_frames)
    {
        if (!encoded_frame.empty())
            parse_rudics_packet(out->add_frame(), encoded_frame + "\r", "\r", false);
    }
}

} // namespace acomms
} // namespace goby
#endif
