// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef BenthosATM900Driver20161221H
#define BenthosATM900Driver20161221H


#include <dccl/field_codec_fixed.h>
#include <dccl/codec.h>
#include <dccl/field_codec_manager.h>

#include "goby/common/time.h"

#include "driver_base.h"
#include "goby/acomms/protobuf/benthos_atm900.pb.h"
#include "goby/acomms/acomms_helpers.h"
#include "benthos_atm900_driver_fsm.h"
#include "rudics_packet.h"

namespace goby
{
    namespace acomms
    {
        class BenthosATM900Driver : public ModemDriverBase
        {
          public:
            BenthosATM900Driver();
            void startup(const protobuf::DriverConfig& cfg);
            void shutdown();            
            void do_work();
            void handle_initiate_transmission(const protobuf::ModemTransmission& m);

        private:
            void receive(const protobuf::ModemTransmission& msg);
            void send(const protobuf::ModemTransmission& msg);
            void try_serial_tx();
            

        private:
            enum { DEFAULT_BAUD = 9600 };
            static const std::string SERIAL_DELIMITER;

            benthos_fsm::BenthosATM900FSM fsm_;
            protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
            goby::uint32 next_frame_;
        };


        // placeholder id codec that uses no bits, since we're always sending just this message on the wire
        class NoOpIdentifierCodec : public dccl::TypedFixedFieldCodec<dccl::uint32>
        {
            dccl::Bitset encode() { return dccl::Bitset(); }
            dccl::Bitset encode(const uint32& wire_value) { return dccl::Bitset(); }
            dccl::uint32 decode(dccl::Bitset* bits) { return 0; }
            virtual unsigned size() { return 0; }

        };

        extern boost::shared_ptr<dccl::Codec> benthos_header_dccl_;
        
        inline void init_benthos_dccl()
        {
            dccl::FieldCodecManager::add<NoOpIdentifierCodec>("benthos_header_id");
            benthos_header_dccl_.reset(new dccl::Codec("benthos_header_id"));
            benthos_header_dccl_->load<benthos::protobuf::BenthosHeader>();
        }
        
        inline void serialize_benthos_modem_message(std::string* out, const goby::acomms::protobuf::ModemTransmission& in)
        {
            benthos::protobuf::BenthosHeader header;
            header.set_type(in.type());
            if(in.has_ack_requested())
                header.set_ack_requested(in.ack_requested());

            for(int i = 0, n = in.acked_frame_size(); i < n; ++i)
                header.add_acked_frame(in.acked_frame(i));

            benthos_header_dccl_->encode(out, header);

            // frame message
            for(int i = 0, n = in.frame_size(); i < n; ++i)
            {
                if(in.frame(i).empty())
                    break;
                
                std::string rudics_packet;
                serialize_rudics_packet(in.frame(i), &rudics_packet, "\r", false);
                *out += rudics_packet;
            }            
        }

        inline void parse_benthos_modem_message(std::string in, goby::acomms::protobuf::ModemTransmission* out)
        {
            benthos::protobuf::BenthosHeader header;
            benthos_header_dccl_->decode(&in, &header);

            out->set_type(header.type());
            if(header.has_ack_requested())
                out->set_ack_requested(header.ack_requested());

            for(int i = 0, n = header.acked_frame_size(); i < n; ++i)
                out->add_acked_frame(header.acked_frame(i));

            std::vector<std::string> encoded_frames;
            boost::split(encoded_frames, in, boost::is_any_of("\r"), boost::token_compress_on);

            for(int i = 0, n = encoded_frames.size(); i < n; ++i)
            {
                if(!encoded_frames[i].empty())
                    parse_rudics_packet(out->add_frame(), encoded_frames[i] + "\r", "\r", false);
            }            
        }
        
    }
}
#endif
