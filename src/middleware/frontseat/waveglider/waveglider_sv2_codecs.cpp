// Copyright 2017-2020:
//   GobySoft, LLC (2013-)
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

#include <dccl/codecs3/field_codec_default.h>

#include "waveglider_sv2_codecs.h"
#include "waveglider_sv2_frontseat_driver.pb.h"

extern "C"
{
    void dccl3_load(dccl::Codec* dccl)
    {
        using namespace dccl;

        FieldCodecManager::add<goby::middleware::frontseat::SV2IdentifierCodec>("SV2.id");
        FieldCodecManager::add<dccl::v3::DefaultMessageCodec,
                               google::protobuf::FieldDescriptor::TYPE_MESSAGE>("SV2");
        FieldCodecManager::add<dccl::v3::DefaultBytesCodec,
                               google::protobuf::FieldDescriptor::TYPE_BYTES>("SV2");
        FieldCodecManager::add<goby::middleware::frontseat::SV2NumericCodec<dccl::uint32>>("SV2");

        dccl->load<goby::middleware::protobuf::SV2RequestEnumerate>();
        dccl->load<goby::middleware::protobuf::SV2ReplyEnumerate>();
        dccl->load<goby::middleware::protobuf::SV2RequestStatus>();
        dccl->load<goby::middleware::protobuf::SV2ReplyStatus>();
        dccl->load<goby::middleware::protobuf::SV2RequestQueuedMessage>();
        dccl->load<goby::middleware::protobuf::SV2ReplyQueuedMessage>();
        dccl->load<goby::middleware::protobuf::SV2ACKNAKQueuedMessage>();
        dccl->load<goby::middleware::protobuf::SV2GenericNAK>();
        dccl->load<goby::middleware::protobuf::SV2GenericACK>();
        dccl->load<goby::middleware::protobuf::SV2SendToConsole>();
        dccl->load<goby::middleware::protobuf::SV2CommandFollowFixedHeading>();
        dccl->load<goby::middleware::protobuf::SV2CommandFollowFixedHeading::
                       CommandFollowFixedHeadingBody>();
    }

    void dccl3_unload(dccl::Codec* dccl)
    {
        using namespace dccl;

        dccl->unload<goby::middleware::protobuf::SV2RequestEnumerate>();
        dccl->unload<goby::middleware::protobuf::SV2ReplyEnumerate>();
        dccl->unload<goby::middleware::protobuf::SV2RequestStatus>();
        dccl->unload<goby::middleware::protobuf::SV2ReplyStatus>();
        dccl->unload<goby::middleware::protobuf::SV2RequestQueuedMessage>();
        dccl->unload<goby::middleware::protobuf::SV2ReplyQueuedMessage>();
        dccl->unload<goby::middleware::protobuf::SV2ACKNAKQueuedMessage>();
        dccl->unload<goby::middleware::protobuf::SV2GenericNAK>();
        dccl->unload<goby::middleware::protobuf::SV2GenericACK>();
        dccl->unload<goby::middleware::protobuf::SV2SendToConsole>();
        dccl->unload<goby::middleware::protobuf::SV2CommandFollowFixedHeading>();
        dccl->unload<goby::middleware::protobuf::SV2CommandFollowFixedHeading::
                         CommandFollowFixedHeadingBody>();

        FieldCodecManager::remove<goby::middleware::frontseat::SV2IdentifierCodec>("SV2.id");
        FieldCodecManager::remove<dccl::v3::DefaultMessageCodec,
                                  google::protobuf::FieldDescriptor::TYPE_MESSAGE>("SV2");
        FieldCodecManager::remove<dccl::v3::DefaultBytesCodec,
                                  google::protobuf::FieldDescriptor::TYPE_BYTES>("SV2");
        FieldCodecManager::remove<goby::middleware::frontseat::SV2NumericCodec<dccl::uint32>>(
            "SV2");
    }
}
