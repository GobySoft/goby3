// Copyright 2019-2021:
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

#ifndef GOBY_MIDDLEWARE_MARSHALLING_DETAIL_DCCL_SERIALIZER_PARSER_H
#define GOBY_MIDDLEWARE_MARSHALLING_DETAIL_DCCL_SERIALIZER_PARSER_H

#include <memory>        // for unique_ptr
#include <mutex>         // for mutex, lock_guard
#include <ostream>       // for basic_ostream
#include <set>           // for set
#include <string>        // for string, operat...
#include <unordered_map> // for unordered_map
#include <utility>       // for pair, make_pair

#include <dccl/codec.h>                    // for Codec
#include <dccl/dynamic_protobuf_manager.h> // for DynamicProtobu...

#include "goby/middleware/protobuf/intervehicle.pb.h" // for DCCLForwardedData
#include "goby/util/debug_logger/flex_ostream.h"      // for operator<<

namespace google
{
namespace protobuf
{
class Descriptor;
} // namespace protobuf
} // namespace google

namespace goby
{
namespace middleware
{
namespace protobuf
{
class SerializerProtobufMetadata;
} // namespace protobuf

namespace detail
{
/// \brief Wraps a dccl::Codec in a thread-safe way to make it usable by SerializerParserHelper
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
        ~Loader() override { codec().unload<DataType>(); }
    };

    struct LoaderDynamic : public LoaderBase
    {
        LoaderDynamic(const google::protobuf::Descriptor* desc) : desc_(desc)
        {
            codec().load(desc_);
        }
        ~LoaderDynamic() override { codec().unload(desc_); }

      private:
        const google::protobuf::Descriptor* desc_;
    };

    static std::unordered_map<const google::protobuf::Descriptor*, std::unique_ptr<LoaderBase>>
        loader_map_;

    static std::set<std::string> loaded_proto_files_;

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
            codec_ = std::make_unique<dccl::Codec>();
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

    constexpr static int INVALID_DCCL_ID{0};

    template <typename CharIterator> static unsigned id(CharIterator begin, CharIterator end)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        return codec().id(begin, end);
    }

    static unsigned id(const std::string& full_name)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        auto* desc = dccl::DynamicProtobufManager::find_descriptor(full_name);
        if (desc)
        {
            return codec().id(desc);
        }
        else
        {
            goby::glog.is_warn() && goby::glog << "No DCCL message found with name: " << full_name
                                               << std::endl;
            return 0;
        }
    }

    static void load_metadata(const goby::middleware::protobuf::SerializerProtobufMetadata& meta);
    static goby::middleware::intervehicle::protobuf::DCCLForwardedData
    unpack(const std::string& bytes);

    static void load_library(const std::string& library)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        codec().load_library(library);
    }

    /// \brief Enable dlog output to glog using same verbosity settings as glog.
    static void setup_dlog();
};
} // namespace detail

} // namespace middleware
} // namespace goby

#endif
