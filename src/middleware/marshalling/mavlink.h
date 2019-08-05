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

#ifndef MarshallingMAVLink20190718H
#define MarshallingMAVLink20190718H

#include "interface.h"

#include "goby/exception.h"
#include "goby/util/debug_logger.h"

#include <mavlink/v2.0/standard/standard.hpp>

namespace goby
{
namespace middleware
{
// must register the MESSAGE_ENTRIES for the dialect(s) you're using with this registry, if other than standard and minimal
struct MAVLinkRegistry
{
    template <std::size_t Size>
    static void register_dialect_entries(std::array<mavlink::mavlink_msg_entry_t, Size> entries)
    {
        std::lock_guard<std::mutex> lock(mavlink_registry_mutex_);
        for (const auto& entry : entries) entries_.insert(std::make_pair(entry.msgid, entry));
    }

    static const mavlink::mavlink_msg_entry_t* get_msg_entry(uint32_t msgid)
    {
        if (entries_.empty())
            register_default_dialects();

        std::lock_guard<std::mutex> lock(mavlink_registry_mutex_);
        auto it = entries_.find(msgid);
        if (it != entries_.end())
            return &it->second;
        else
            return nullptr;
    }

    static void register_default_dialects();

  private:
    static std::unordered_map<uint32_t, mavlink::mavlink_msg_entry_t> entries_;
    static std::mutex mavlink_registry_mutex_;
};

// runtime introspection google::protobuf::Message (publish only)
template <> struct SerializerParserHelper<mavlink::mavlink_message_t, MarshallingScheme::MAVLINK>
{
    static std::vector<char> serialize(const mavlink::mavlink_message_t& msg)
    {
        std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> buffer;
        auto length = mavlink::mavlink_msg_to_send_buffer(&buffer[0], &msg);
        return std::vector<char>(buffer.begin(), buffer.begin() + length);
    }

    static std::string type_name(const mavlink::mavlink_message_t& msg)
    {
        return std::to_string(msg.msgid);
    }

    template <typename CharIterator>
    static std::shared_ptr<mavlink::mavlink_message_t>
    parse_dynamic(CharIterator bytes_begin, CharIterator bytes_end, CharIterator& actual_end,
                  const std::string& type)
    {
        CharIterator c = bytes_begin;
        auto msg = std::make_shared<mavlink::mavlink_message_t>();
        mavlink::mavlink_message_t msg_buffer{};
        mavlink::mavlink_status_t status{}, status_buffer{};
        while (c != bytes_end)
        {
            auto res = mavlink::mavlink_frame_char_buffer(&msg_buffer, &status_buffer, *c,
                                                          msg.get(), &status);

            switch (res)
            {
                case mavlink::MAVLINK_FRAMING_INCOMPLETE: ++c; break;

                case mavlink::MAVLINK_FRAMING_OK:
                {
                    actual_end = c;
                    return msg;
                }

                case mavlink::MAVLINK_FRAMING_BAD_CRC:
                    goby::glog.is_warn() && goby::glog << "BAD CRC decoding MAVLink type: " << type
                                                       << std::endl;
                    goto fail;
                    break;
                case mavlink::MAVLINK_FRAMING_BAD_SIGNATURE:
                    goby::glog.is_warn() &&
                        goby::glog << "BAD SIGNATURE decoding MAVLink type: " << type << std::endl;
                    goto fail;

                    break;
                default:
                    goby::glog.is_warn() && goby::glog
                                                << "Unknown value " << res
                                                << " returned while decoding MAVLink type: " << type
                                                << std::endl;
                    goto fail;

                    break;
            }
        }
    fail:
        actual_end = bytes_end;
        return msg;
    }
};

// user mavlink (static), e.g. DataType == HEARTBEAT
template <typename DataType> struct SerializerParserHelper<DataType, MarshallingScheme::MAVLINK>
{
    static std::vector<char> serialize(const DataType& packet)
    {
        mavlink::mavlink_message_t msg{};
        mavlink::MsgMap map(msg);
        packet.serialize(map);
        mavlink::mavlink_finalize_message(&msg, 1, 1, packet.MIN_LENGTH, packet.LENGTH,
                                          packet.CRC_EXTRA);
        return SerializerParserHelper<mavlink::mavlink_message_t,
                                      MarshallingScheme::MAVLINK>::serialize(msg);
    }

    // use numeric type name since that's all we have with mavlink_message_t alone
    static std::string type_name() { return std::to_string(DataType::MSG_ID); }
    static std::string type_name(const DataType& d) { return type_name(); }

    template <typename CharIterator>
    static std::shared_ptr<DataType> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                           CharIterator& actual_end)
    {
        auto msg =
            SerializerParserHelper<mavlink::mavlink_message_t,
                                   MarshallingScheme::MAVLINK>::parse_dynamic(bytes_begin,
                                                                              bytes_end, actual_end,
                                                                              type_name());
        auto packet = std::make_shared<DataType>();
        mavlink::MsgMap map(msg.get());
        packet->deserialize(map);
        return packet;
    }

}; // namespace middleware

template <typename T, typename std::enable_if<
                          std::is_base_of<mavlink::Message, T>::value ||
                          std::is_same<mavlink::mavlink_message_t, T>::value>::type* = nullptr>
constexpr int scheme()
{
    return goby::middleware::MarshallingScheme::MAVLINK;
}

} // namespace middleware
} // namespace goby

#endif
