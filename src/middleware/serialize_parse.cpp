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

#include "goby/middleware/protobuf/interprocess_data.pb.h"
#include "goby/common/logger.h"

#include "serialize_parse.h"


std::map<int, std::string> goby::MarshallingScheme::e2s =
{ {CSTR, "CSTR"},
  {PROTOBUF, "PROTOBUF"},
  {DCCL, "DCCL"},
  {CAPTN_PROTO, "CAPTN_PROTO"},
  {MSGPACK, "MSGPACK"} };

std::unique_ptr<dccl::Codec> goby::DCCLSerializerParserHelperBase::codec_(nullptr);
std::unordered_map<const google::protobuf::Descriptor*, std::unique_ptr<goby::DCCLSerializerParserHelperBase::LoaderBase>> goby::DCCLSerializerParserHelperBase::loader_map_;
std::mutex goby::DCCLSerializerParserHelperBase::dccl_mutex_;

void goby::DCCLSerializerParserHelperBase::load_forwarded_subscription(const goby::protobuf::DCCLSubscription& sub)
{
    std::lock_guard<std::mutex> lock(dccl_mutex_);

    // check that we don't already have this type available
    if(auto* desc = dccl::DynamicProtobufManager::find_descriptor(sub.protobuf_name()))
    {
        check_load(desc);
    }
    else
    {
        for(const auto& file_desc : sub.file_descriptor())
            dccl::DynamicProtobufManager::add_protobuf_file(file_desc);
        if(auto* desc = dccl::DynamicProtobufManager::find_descriptor(sub.protobuf_name()))
            check_load(desc);
        else
            goby::glog.is(goby::common::logger::DEBUG3) && goby::glog << "Failed to load DCCL message sent via forwarded subscription: " << sub.protobuf_name() << std::endl;
    }
}

goby::protobuf::DCCLForwardedData goby::DCCLSerializerParserHelperBase::unpack(const std::string& frame)
{
    std::lock_guard<std::mutex> lock(dccl_mutex_);
    
    goby::protobuf::DCCLForwardedData packets;

    std::string::const_iterator frame_it = frame.begin(), frame_end = frame.end();
    while(frame_it < frame_end)
    {
        auto dccl_id = codec().id(frame_it, frame_end);

        goby::protobuf::DCCLPacket& packet = *packets.add_frame();
        packet.set_dccl_id(dccl_id);
        
        std::string::const_iterator next_frame_it;

        if(codec().loaded().count(dccl_id) == 0)
        {
            goby::glog.is_debug1() && goby::glog << "DCCL ID " << dccl_id << " is not loaded. Discarding remainder of the message." << std::endl;
            packets.mutable_frame()->RemoveLast();
            return packets;
        }
        
        const auto* desc = codec().loaded().at(dccl_id);
        auto msg = dccl::DynamicProtobufManager::new_protobuf_message<std::unique_ptr<google::protobuf::Message>>(desc);
            
        next_frame_it = codec().decode(frame_it, frame_end, msg.get());
        packet.set_data(std::string(frame_it, next_frame_it));

        frame_it = next_frame_it;        
    }
    
    return packets;
}



