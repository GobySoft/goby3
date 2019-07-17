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

#ifndef SerializeParseDCCL20190717H
#define SerializeParseDCCL20190717H

#include "serialize_parse_protobuf.h"

namespace goby
{
namespace middleware
{
namespace protobuf
{
class InterVehicleSubscription;
class DCCLForwardedData;
} // namespace protobuf
struct DCCLSerializerParserHelperBase
{
  private:
    static std::unique_ptr<dccl::Codec> codec_;

  protected:
    static std::mutex dccl_mutex_;

    struct LoaderBase
    {
        LoaderBase() = default;
        virtual ~LoaderBase() = default;
    };

    template <typename DataType> struct Loader : public LoaderBase
    {
        Loader() { codec().load<DataType>(); }
        ~Loader() { codec().unload<DataType>(); }
    };

    struct LoaderDynamic : public LoaderBase
    {
        LoaderDynamic(const google::protobuf::Descriptor* desc) : desc_(desc)
        {
            codec().load(desc_);
        }
        ~LoaderDynamic() { codec().unload(desc_); }

      private:
        const google::protobuf::Descriptor* desc_;
    };

    static std::unordered_map<const google::protobuf::Descriptor*, std::unique_ptr<LoaderBase>>
        loader_map_;

    template <typename DataType> static void check_load()
    {
        const auto* desc = DataType::descriptor();
        if (!loader_map_.count(desc))
            loader_map_.insert(
                std::make_pair(desc, std::unique_ptr<LoaderBase>(new Loader<DataType>())));
    }

    static void check_load(const google::protobuf::Descriptor* desc)
    {
        if (!loader_map_.count(desc))
            loader_map_.insert(
                std::make_pair(desc, std::unique_ptr<LoaderBase>(new LoaderDynamic(desc))));
    }

    static dccl::Codec& codec()
    {
        if (!codec_)
            codec_.reset(new dccl::Codec);
        return *codec_;
    }

    static dccl::Codec& set_codec(dccl::Codec* new_codec)
    {
        codec_.reset(new_codec);
        loader_map_.clear();
        return *new_codec;
    }

  public:
    DCCLSerializerParserHelperBase() = default;
    virtual ~DCCLSerializerParserHelperBase() = default;

    template <typename CharIterator> static unsigned id(CharIterator begin, CharIterator end)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        return codec().id(begin, end);
    }

    static unsigned id(const std::string full_name)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        auto* desc = dccl::DynamicProtobufManager::find_descriptor(full_name);
        if (desc)
            return codec().id(desc);
        else
            return 0;
    }

    static void
    load_forwarded_subscription(const goby::middleware::intervehicle::protobuf::Subscription& sub);
    static goby::middleware::intervehicle::protobuf::DCCLForwardedData
    unpack(const std::string& bytes);

    static void load_library(const std::string& library)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        codec().load_library(library);
    }
};

template <typename DataType>
struct SerializerParserHelper<DataType, MarshallingScheme::DCCL>
    : public DCCLSerializerParserHelperBase
{
  public:
    static std::vector<char> serialize(const DataType& msg)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load<DataType>();
        std::vector<char> bytes(codec().size(msg), 0);
        codec().encode(bytes.data(), bytes.size(), msg);
        return bytes;
    }

    static std::string type_name() { return DataType::descriptor()->full_name(); }
    static std::string type_name(const DataType& d) { return type_name(); }

    template <typename CharIterator>
    static std::shared_ptr<DataType> parse(CharIterator bytes_begin, CharIterator bytes_end,
                                           CharIterator& actual_end)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load<DataType>();
        auto msg = std::make_shared<DataType>();
        actual_end = codec().decode(bytes_begin, bytes_end, msg.get());
        return msg;
    }

    static unsigned id()
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load<DataType>();
        return codec().template id<DataType>();
    }

  private:
};

template <>
struct SerializerParserHelper<google::protobuf::Message, MarshallingScheme::DCCL>
    : public DCCLSerializerParserHelperBase
{
  public:
    static std::vector<char> serialize(const google::protobuf::Message& msg)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load(msg.GetDescriptor());
        std::vector<char> bytes(codec().size(msg), 0);
        codec().encode(bytes.data(), bytes.size(), msg);
        return bytes;
    }

    static std::string type_name(const google::protobuf::Descriptor* desc)
    {
        return desc->full_name();
    }
    static std::string type_name(const google::protobuf::Message& d)
    {
        return type_name(d.GetDescriptor());
    }

    template <typename CharIterator>
    static std::shared_ptr<google::protobuf::Message>
    parse_dynamic(CharIterator bytes_begin, CharIterator bytes_end, CharIterator& actual_end,
                  const std::string& type)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);

        auto msg = dccl::DynamicProtobufManager::new_protobuf_message<
            std::shared_ptr<google::protobuf::Message>>(type);

        check_load(msg->GetDescriptor());
        actual_end = codec().decode(bytes_begin, bytes_end, msg.get());
        return msg;
    }

    static unsigned id(const google::protobuf::Descriptor* desc)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load(desc);
        return codec().id(desc);
    }

  private:
};

} // namespace middleware
} // namespace goby

#endif
