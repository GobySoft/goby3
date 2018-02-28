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

#ifndef IridiumDriverCommon20150508H
#define IridiumDriverCommon20150508H

#include <dccl/field_codec_fixed.h>
#include <dccl/codec.h>
#include <dccl/field_codec_manager.h>

#include "goby/acomms/protobuf/iridium_driver.pb.h"

namespace goby
{
    namespace acomms
    {
        enum { RATE_RUDICS = 1, RATE_SBD = 0 };    

        class OnCallBase
        {
          public:
          OnCallBase()
              : last_tx_time_(goby::common::goby_time<double>()),
                last_rx_time_(0),
                bye_received_(false),
                bye_sent_(false),
                total_bytes_sent_(0),
                last_bytes_sent_(0)
                { }
            double last_rx_tx_time() const { return std::max(last_tx_time_, last_rx_time_); }
            double last_rx_time() const { return last_rx_time_; }
            double last_tx_time() const { return last_tx_time_; }

            int last_bytes_sent() const { return last_bytes_sent_; }
            int total_bytes_sent() const { return total_bytes_sent_; }
            
                
            void set_bye_received(bool b) { bye_received_ = b; }
            void set_bye_sent(bool b) { bye_sent_ = b; }
                
            bool bye_received() const { return bye_received_; }
            bool bye_sent() const { return bye_sent_; }
                
            void set_last_tx_time(double d) { last_tx_time_ = d; }
            void set_last_rx_time(double d) { last_rx_time_ = d; }

            void set_last_bytes_sent(int i) 
            {
                last_bytes_sent_ = i;
                total_bytes_sent_ += i;
            }
            
          private:
            double last_tx_time_;            
            double last_rx_time_;            
            bool bye_received_;
            bool bye_sent_;
            int total_bytes_sent_;
            int last_bytes_sent_;
        };


        // placeholder id codec that uses no bits, since we're always sending just this message on the wire
        class IridiumHeaderIdentifierCodec : public dccl::TypedFixedFieldCodec<dccl::uint32>
        {
            dccl::Bitset encode() { return dccl::Bitset(); }
            dccl::Bitset encode(const uint32& wire_value) { return dccl::Bitset(); }
            dccl::uint32 decode(dccl::Bitset* bits) { return 0; }
            virtual unsigned size() { return 0; }

        };

        extern boost::shared_ptr<dccl::Codec> iridium_header_dccl_;
        
        inline void init_iridium_dccl()
        {
            dccl::FieldCodecManager::add<IridiumHeaderIdentifierCodec>("iridium_header_id");
            iridium_header_dccl_.reset(new dccl::Codec("iridium_header_id"));
            iridium_header_dccl_->load<IridiumHeader>();
        }
        
        inline void serialize_iridium_modem_message(std::string* out, const goby::acomms::protobuf::ModemTransmission& in)
        {
            IridiumHeader header;
            header.set_src(in.src());
            header.set_dest(in.dest());
            if(in.has_rate())
                header.set_rate(in.rate());
            header.set_type(in.type());
            if(in.has_ack_requested())
                header.set_ack_requested(in.ack_requested());
            if(in.has_frame_start())
                header.set_frame_start(in.frame_start());
            if(in.acked_frame_size())
                header.set_acked_frame(in.acked_frame(0));

            iridium_header_dccl_->encode(out, header);
            if(in.frame_size())
                *out += in.frame(0);
        }

        inline void parse_iridium_modem_message(std::string in, goby::acomms::protobuf::ModemTransmission* out)
        {
            IridiumHeader header;
            iridium_header_dccl_->decode(&in, &header);
            
            out->set_src(header.src());
            out->set_dest(header.dest());
            if(header.has_rate())
                out->set_rate(header.rate());
            out->set_type(header.type());
            if(header.has_ack_requested())
                out->set_ack_requested(header.ack_requested());
            if(header.has_frame_start())
                out->set_frame_start(header.frame_start());
            if(header.has_acked_frame())
                out->add_acked_frame(header.acked_frame());

            if(in.size())
                out->add_frame(in);
        }
        
    }
}

#endif
