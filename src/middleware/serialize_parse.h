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

#ifndef SerializeParse20160607H
#define SerializeParse20160607H

#include <map>
#include <mutex>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include <google/protobuf/message.h>

#include <dccl.h>

namespace goby
{
namespace middleware
{
//
// MarshallingScheme
//

struct MarshallingScheme
{
    enum MarshallingSchemeEnum
    {
        ALL_SCHEMES = -2,
        NULL_SCHEME = -1,
        CSTR = 0,
        PROTOBUF = 1,
        DCCL = 2,
        CAPTN_PROTO = 3,
        MSGPACK = 4,
        CXX_OBJECT = 5
    };

    static std::string as_string(int e)
    {
        auto it = e2s.find(e);
        return it != e2s.end() ? it->second : std::to_string(e);
    }

  private:
    static std::map<int, std::string> e2s;
};

//
// SerializerParserHelper
//

template <typename DataType, int scheme, class Enable = void> struct SerializerParserHelper
{
};

template <typename DataType> struct SerializerParserHelper<DataType, MarshallingScheme::CSTR>
{
    static std::vector<char> serialize(const DataType& msg)
    {
        std::vector<char> bytes(msg.begin(), msg.end());
        bytes.push_back('\0');
        return bytes;
    }

    static std::string type_name() { return "CSTR"; }

    static std::string type_name(const DataType& d) { return type_name(); }

    template <typename CharIterator>
    static DataType parse(CharIterator bytes_begin, CharIterator bytes_end,
                          CharIterator& actual_end)
    {
        actual_end = bytes_end;
        if (bytes_begin != bytes_end)
        {
            DataType msg(bytes_begin, bytes_end - 1);
            return msg;
        }
        else
        {
            return DataType();
        }
    }
};

// user protobuf (static), e.g. DataType == Foo for "message Foo"
template <typename DataType>
struct SerializerParserHelper<
    DataType, MarshallingScheme::PROTOBUF,
    std::enable_if_t<!std::is_same<DataType, google::protobuf::Message>::value> >
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
    static DataType parse(CharIterator bytes_begin, CharIterator bytes_end,
                          CharIterator& actual_end)
    {
        DataType msg;
        msg.ParseFromArray(&*bytes_begin, bytes_end - bytes_begin);
        actual_end = bytes_begin + msg.ByteSize();
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
    static void parse(CharIterator bytes_begin, CharIterator bytes_end, CharIterator& actual_end,
                      google::protobuf::Message* msg)
    {
        msg->ParseFromArray(&*bytes_begin, bytes_end - bytes_begin);
        actual_end = bytes_begin + msg->ByteSize();
    }
};

namespace protobuf
{
class DCCLSubscription;
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

    static std::unordered_map<const google::protobuf::Descriptor*, std::unique_ptr<LoaderBase> >
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

    static void
    load_forwarded_subscription(const goby::middleware::protobuf::DCCLSubscription& sub);
    static goby::middleware::protobuf::DCCLForwardedData unpack(const std::string& bytes);

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
    static DataType parse(CharIterator bytes_begin, CharIterator bytes_end,
                          CharIterator& actual_end)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load<DataType>();
        DataType msg;
        actual_end = codec().decode(bytes_begin, bytes_end, &msg);
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
    static void parse(CharIterator bytes_begin, CharIterator bytes_end, CharIterator& actual_end,
                      google::protobuf::Message* msg)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load(msg->GetDescriptor());
        actual_end = codec().decode(bytes_begin, bytes_end, msg);
    }

    static unsigned id(const google::protobuf::Descriptor* desc)
    {
        std::lock_guard<std::mutex> lock(dccl_mutex_);
        check_load(desc);
        return codec().id(desc);
    }

  private:
};

//
// scheme
//

template <typename T> struct primitive_type
{
    typedef T type;
};

template <typename T> struct primitive_type<std::shared_ptr<T> >
{
    typedef T type;
};

template <typename T> struct primitive_type<std::shared_ptr<const T> >
{
    typedef T type;
};

template <typename T, typename Transporter> constexpr int transporter_scheme()
{
    return Transporter::template scheme<typename primitive_type<T>::type>();
}

template <typename T, typename std::enable_if<std::is_same<T, std::string>::value>::type* = nullptr>
constexpr int scheme()
{
    return goby::middleware::MarshallingScheme::CSTR;
}

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
} // namespace goby
} // namespace goby

#endif
