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

#ifndef SerializeParseProtobuf20190717H
#define SerializeParseProtobuf20190717H

#include "serialize_parse.h"

#include <google/protobuf/message.h>

#include "goby/middleware/protobuf/intervehicle.pb.h"

namespace goby
{
namespace middleware
{
// user protobuf (static), e.g. DataType == Foo for "message Foo"
template <typename DataType>
struct SerializerParserHelper<
    DataType, MarshallingScheme::PROTOBUF,
    std::enable_if_t<!std::is_same<DataType, google::protobuf::Message>::value>>
{
    static std::vector<char> serialize(const DataType& msg)
    {
        std::vector<char> bytes(msg.ByteSize(), 0);
        msg.SerializeToArray(bytes.data(), bytes.size());
        return bytes;
    }

    static std::string type_name() { return DataType::descriptor()->full_name(); }
    static std::string type_name(const DataType& d) { return type_name(); }

    template <typename CharIterator>
    static std::shared_ptr<DataType> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                           CharIterator& actual_end)
    {
        auto msg = std::make_shared<DataType>();
        msg->ParseFromArray(&*bytes_begin, bytes_end - bytes_begin);
        actual_end = bytes_begin + msg->ByteSize();
        return msg;
    }
};

// runtime introspection google::protobuf::Message (publish only)
template <> struct SerializerParserHelper<google::protobuf::Message, MarshallingScheme::PROTOBUF>
{
    static std::vector<char> serialize(const google::protobuf::Message& msg)
    {
        std::vector<char> bytes(msg.ByteSize(), 0);
        msg.SerializeToArray(bytes.data(), bytes.size());
        return bytes;
    }

    static std::string type_name(const google::protobuf::Message& d)
    {
        return d.GetDescriptor()->full_name();
    }

    // Must subscribe to the actual type (or use subscribe_regex())
    static std::string type_name(const google::protobuf::Descriptor* desc)
    {
        return desc->full_name();
    }

    template <typename CharIterator>
    static std::shared_ptr<google::protobuf::Message>
    parse_dynamic(CharIterator bytes_begin, CharIterator bytes_end, CharIterator& actual_end,
                  const std::string& type)
    {
        std::shared_ptr<google::protobuf::Message> msg;

        {
            static std::mutex dynamic_protobuf_manager_mutex;
            std::lock_guard<std::mutex> lock(dynamic_protobuf_manager_mutex);
            msg = dccl::DynamicProtobufManager::new_protobuf_message<
                std::shared_ptr<google::protobuf::Message>>(type);
        }

        msg->ParseFromArray(&*bytes_begin, bytes_end - bytes_begin);
        actual_end = bytes_begin + msg->ByteSize();
        return msg;
    }
};

namespace protobuf
{
namespace detail
{
// used to select between DCCL messages (with added DCCLParameters Enumeration)
// and normal Protobuf messages
// in the DCCL case, both "scheme_protobuf_or_dccl" functions are valid, but the one
// with "dccl_selector" as the argument is chosen because this doesn't require
// upcasting to "protobuf_selector"
// in the plain Protobuf case, just the "scheme_protobuf_or_dccl(protobuf_selector)" function
// is chosen by template resolution, so this one is used.
struct protobuf_selector
{
};
struct dccl_selector : protobuf_selector
{
};

template <typename T,
          typename std::enable_if<std::is_enum<typename T::DCCLParameters>::value>::type* = nullptr>
constexpr int scheme_protobuf_or_dccl(dccl_selector)
{
    return goby::middleware::MarshallingScheme::DCCL;
}

template <typename T> constexpr int scheme_protobuf_or_dccl(protobuf_selector)
{
    return goby::middleware::MarshallingScheme::PROTOBUF;
}
} // namespace detail
} // namespace protobuf

template <typename T, typename std::enable_if<
                          std::is_base_of<google::protobuf::Message, T>::value>::type* = nullptr>
constexpr int scheme()
{
    return protobuf::detail::scheme_protobuf_or_dccl<T>(protobuf::detail::dccl_selector());
}

} // namespace middleware
} // namespace goby

#endif
