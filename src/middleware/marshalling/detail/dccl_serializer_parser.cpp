// Copyright 2016-2023:
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

#include <list> // for oper...
#include <map>  // for map

#include <dccl/logger.h>                   // for Logger
#include <google/protobuf/descriptor.pb.h> // for File...
#include <google/protobuf/message.h>       // for Message

#include "goby/middleware/protobuf/intervehicle.pb.h"           // for DCCL...
#include "goby/middleware/protobuf/serializer_transporter.pb.h" // for Seri...
#include "goby/util/debug_logger/flex_ostreambuf.h"             // for DEBUG3
#include "goby/util/debug_logger/logger_manipulators.h"         // for oper...
#include "goby/util/debug_logger/term_color.h"                  // for Colors
#include "goby/version.h"

#include "dccl_serializer_parser.h"

using goby::glog;

namespace google
{
namespace protobuf
{
class Descriptor;
} // namespace protobuf
} // namespace google

std::unique_ptr<dccl::Codec>
    goby::middleware::detail::DCCLSerializerParserHelperBase::codec_(nullptr);
std::unordered_map<
    const google::protobuf::Descriptor*,
    std::unique_ptr<goby::middleware::detail::DCCLSerializerParserHelperBase::LoaderBase>>
    goby::middleware::detail::DCCLSerializerParserHelperBase::loader_map_;
std::mutex goby::middleware::detail::DCCLSerializerParserHelperBase::dccl_mutex_;
std::set<std::string> goby::middleware::detail::DCCLSerializerParserHelperBase::loaded_proto_files_;

void goby::middleware::detail::DCCLSerializerParserHelperBase::load_metadata(
    const goby::middleware::protobuf::SerializerProtobufMetadata& meta)
{
    std::lock_guard<std::mutex> lock(dccl_mutex_);

    // check that we don't already have this type available
    if (auto* desc = dccl::DynamicProtobufManager::find_descriptor(meta.protobuf_name()))
    {
        check_load(desc);
    }
    else
    {
        for (const auto& file_desc_proto : meta.file_descriptor())
        {
            if (!loaded_proto_files_.count(file_desc_proto.name()))
            {
                dccl::DynamicProtobufManager::add_protobuf_file(file_desc_proto);
                loaded_proto_files_.insert(file_desc_proto.name());
            }
        }

        if (auto* desc = dccl::DynamicProtobufManager::find_descriptor(meta.protobuf_name()))
            check_load(desc);
        else
            glog.is(goby::util::logger::DEBUG3) &&
                glog << "Failed to load DCCL message via metadata: " << meta.protobuf_name()
                     << std::endl;
    }
}

void check_subscription_version(unsigned dccl_id, const google::protobuf::Message& msg)
{
    if (dccl_id == goby::middleware::intervehicle::protobuf::SUBSCRIPTION_DCCL_ID__GOBY_3_1)
    {
        goby::middleware::intervehicle::protobuf::Subscription subscription;
        subscription.CopyFrom(msg);

        glog.is_debug2() && glog << "Checking subscription: " << subscription.ShortDebugString()
                                 << std::endl;

        if (subscription.api_version() != GOBY_INTERVEHICLE_API_VERSION)
        {
            glog.is_warn() &&
                glog << "Received subscription forwarding subscription with incompatible "
                        "GOBY_INTERVEHICLE_API_VERSION (this system: GOBY_INTERVEHICLE_API_VERSION="
                     << GOBY_INTERVEHICLE_API_VERSION << ", remote system (modem id) "
                     << subscription.header().src()
                     << ": GOBY_INTERVEHICLE_API_VERSION=" << subscription.api_version() << ")"
                     << std::endl;

            if (subscription.api_version() > GOBY_INTERVEHICLE_API_VERSION)
            {
                glog.is_warn() &&
                    glog << "Please update the version of Goby on this system in order "
                            "to communication over intervehicle() with the remote system"
                         << std::endl;
            }
            else
            {
                glog.is_warn() &&
                    glog << "Please update the version of Goby on the remote system in "
                            "order to communication over intervehicle() with this system"
                         << std::endl;
            }
        }
    }
}

goby::middleware::intervehicle::protobuf::DCCLForwardedData
goby::middleware::detail::DCCLSerializerParserHelperBase::unpack(const std::string& frame)
{
    std::lock_guard<std::mutex> lock(dccl_mutex_);

    goby::middleware::intervehicle::protobuf::DCCLForwardedData packets;

    std::string::const_iterator frame_it = frame.begin(), frame_end = frame.end();
    while (frame_it < frame_end)
    {
        auto dccl_id = codec().id(frame_it, frame_end);

        goby::middleware::intervehicle::protobuf::DCCLPacket& packet = *packets.add_frame();
        packet.set_dccl_id(dccl_id);

        std::string::const_iterator next_frame_it;

        // check for old Subscription message and give warning
        if (dccl_id == goby::middleware::intervehicle::protobuf::SUBSCRIPTION_DCCL_ID__GOBY_3_0)
        {
            glog.is_warn() &&
                glog << "Received Subscription from old Goby version 3.0 which is not "
                        "compatible with this newer version of Goby. Update the sender to "
                        "Goby 3.1 or newer to use intervehicle comms with this system."
                     << std::endl;
        }

        if (codec().loaded().count(dccl_id) == 0)
        {
            glog.is_debug1() && glog << "DCCL ID " << dccl_id
                                     << " is not loaded. Discarding remainder of the message."
                                     << std::endl;
            packets.mutable_frame()->RemoveLast();
            return packets;
        }

        const auto* desc = codec().loaded().at(dccl_id);
        auto msg = dccl::DynamicProtobufManager::new_protobuf_message<
            std::unique_ptr<google::protobuf::Message>>(desc);

        try
        {
            next_frame_it = codec().decode(frame_it, frame_end, msg.get());
            check_subscription_version(dccl_id, *msg);
        }
        catch (const std::exception& e)
        {
            glog.is_debug1() &&
                glog << "Failed to decode message (DCCL ID " << dccl_id
                     << "). Discarding remainder of the message. Reason: " << e.what() << std::endl;

            // check partial decode of Subscription message for incompatible version
            check_subscription_version(dccl_id, *msg);

            packets.mutable_frame()->RemoveLast();
            return packets;
        }

        packet.set_data(std::string(frame_it, next_frame_it));
        frame_it = next_frame_it;
    }

    return packets;
}

void goby::middleware::detail::DCCLSerializerParserHelperBase::setup_dlog()
{
    static bool setup_complete = false;

    if (!setup_complete)
    {
        std::string glog_dccl_group = "dccl";

        glog.add_group(glog_dccl_group, util::Colors::lt_magenta);

        auto dlog_lambda =
            [=](const std::string& msg, dccl::logger::Verbosity vrb, dccl::logger::Group /*grp*/)
        {
            switch (vrb)
            {
                case dccl::logger::WARN:
                    glog.is_warn() && glog << group(glog_dccl_group) << msg << std::endl;
                    break;

                case dccl::logger::INFO:
                    glog.is_verbose() && glog << group(glog_dccl_group) << msg << std::endl;
                    break;

                default:
                case dccl::logger::DEBUG1:
                    glog.is_debug1() && glog << group(glog_dccl_group) << msg << std::endl;
                    break;

                case dccl::logger::DEBUG2:
                    glog.is_debug2() && glog << group(glog_dccl_group) << msg << std::endl;
                    break;

                case dccl::logger::DEBUG3:
                    glog.is_debug3() && glog << group(glog_dccl_group) << msg << std::endl;
                    break;
            }
        };

        switch (glog.buf().highest_verbosity())
        {
            default:
            case goby::util::logger::QUIET: break;
            case goby::util::logger::WARN:
                dccl::dlog.connect(dccl::logger::WARN_PLUS, dlog_lambda);
                break;
            case goby::util::logger::VERBOSE:
                dccl::dlog.connect(dccl::logger::INFO_PLUS, dlog_lambda);
                break;
            case goby::util::logger::DEBUG1:
                dccl::dlog.connect(dccl::logger::DEBUG1_PLUS, dlog_lambda);
                break;
            case goby::util::logger::DEBUG2:
                dccl::dlog.connect(dccl::logger::DEBUG2_PLUS, dlog_lambda);
                break;
            case goby::util::logger::DEBUG3:
                dccl::dlog.connect(dccl::logger::DEBUG3_PLUS, dlog_lambda);
                break;
        }

        setup_complete = true;
    }
}
