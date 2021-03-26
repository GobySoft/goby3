// Copyright 2009-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef GOBY_ACOMMS_DCCL_DCCL_H
#define GOBY_ACOMMS_DCCL_DCCL_H

#include <list>    // for list
#include <memory>  // for shared_ptr
#include <ostream> // for endl, ostream
#include <set>     // for set
#include <string>  // for string, oper...

#include <dccl/bitset.h>                              // for Bitset
#include <dccl/codecs2/field_codec_default_message.h> // for DefaultMessa...
#include <dccl/common.h>                              // for int64, uint64
#include <dccl/exception.h>                           // for Exception
#include <dccl/field_codec.h>                         // for FieldCodecBase
#include <dccl/field_codec_fixed.h>                   // for TypedFixedFi...
#include <dccl/field_codec_manager.h>                 // for FieldCodecMa...
#include <dccl/field_codec_typed.h>                   // for RepeatedType...
#include <dccl/internal/field_codec_message_stack.h>
#include <dccl/logger.h>                   // for DECODE, ENCODE
#include <dccl/option_extensions.pb.h>     // for DCCLMessageO...
#include <google/protobuf/descriptor.h>    // for Descriptor
#include <google/protobuf/descriptor.pb.h> // for MessageOptions

#include "dccl/codec.h"                                 // for Codec
#include "dccl/codecs2/field_codec_default.h"           // for DefaultBoolC...
#include "dccl/field_codec_id.h"                        // for DefaultIdent...
#include "goby/acomms/protobuf/dccl.pb.h"               // for DCCLConfig
#include "goby/util/binary.h"                           // for hex_encode
#include "goby/util/debug_logger/flex_ostream.h"        // for operator<<
#include "goby/util/debug_logger/flex_ostreambuf.h"     // for WARN, DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h" // for operator<<

namespace google
{
namespace protobuf
{
class Message;
} // namespace protobuf
} // namespace google

namespace goby
{
namespace acomms
{
typedef dccl::Exception DCCLException;
using DCCLNullValueException = dccl::NullValueException;

using DCCLDefaultIdentifierCodec = dccl::DefaultIdentifierCodec;
template <typename WireType, typename FieldType = WireType>
class DefaultNumericFieldCodec : public dccl::v2::DefaultNumericFieldCodec<WireType, FieldType>
{
};

using DCCLDefaultBoolCodec = dccl::v2::DefaultBoolCodec;
using DCCLDefaultStringCodec = dccl::v2::DefaultStringCodec;
using DCCLDefaultBytesCodec = dccl::v2::DefaultBytesCodec;
using DCCLDefaultEnumCodec = dccl::v2::DefaultEnumCodec;

class MessageHandler : public dccl::internal::MessageStack
{
  public:
    MessageHandler(const google::protobuf::FieldDescriptor* field = nullptr) : MessageStack(field)
    {
    }
    using MessagePart = dccl::MessagePart;
    static const MessagePart HEAD = dccl::HEAD, BODY = dccl::BODY, UNKNOWN = dccl::UNKNOWN;
};

template <typename TimeType> class TimeCodec : public dccl::v2::TimeCodecBase<TimeType, 0>
{
    static_assert(sizeof(TimeCodec) == 0, "TimeCodec must be specialized");
};

template <> class TimeCodec<dccl::uint64> : public dccl::v2::TimeCodecBase<dccl::uint64, 1000000>
{
};
template <> class TimeCodec<dccl::int64> : public dccl::v2::TimeCodecBase<dccl::int64, 1000000>
{
};
template <> class TimeCodec<double> : public dccl::v2::TimeCodecBase<double, 1>
{
};

template <typename T> class StaticCodec : public dccl::v2::StaticCodec<T>
{
};

using DCCLDefaultMessageCodec = dccl::v2::DefaultMessageCodec;

using DCCLFieldCodecBase = dccl::FieldCodecBase;

template <typename WireType, typename FieldType = WireType>
struct DCCLTypedFieldCodec : public dccl::TypedFieldCodec<WireType, FieldType>
{
    using DCCLFieldCodecBase = dccl::FieldCodecBase;
};

template <typename WireType, typename FieldType = WireType>
struct DCCLTypedFixedFieldCodec : public dccl::TypedFixedFieldCodec<WireType, FieldType>
{
    using DCCLFieldCodecBase = dccl::FieldCodecBase;
};

template <typename WireType, typename FieldType = WireType>
struct DCCLRepeatedTypedFieldCodec : public dccl::RepeatedTypedFieldCodec<WireType, FieldType>
{
    using DCCLFieldCodecBase = dccl::FieldCodecBase;
};

using DCCLFieldCodecManager = dccl::FieldCodecManager;
//        typedef dccl::TypedFieldCodec DCCLTypedFieldCodec;
using DCCLFieldCodecManager = dccl::FieldCodecManager;
using FromProtoCppTypeBase = dccl::internal::FromProtoCppTypeBase;
//       typedef dccl::FromProtoType FromProtoType;
// typedef dccl::FromProtoCppType FromProtoCppType;
//typedef dccl::ToProtoCppType ToProtoCppType;
using Bitset = dccl::Bitset;
using DCCLTypeHelper = dccl::internal::TypeHelper;

class DCCLCodec
{
  public:
    /// \brief DCCLCodec is a singleton class; use this to get a pointer to the class.
    static DCCLCodec* get()
    {
        static DCCLCodec d;
        return &d;
    }

    /// \brief Return the underlying dccl::Codec that is used by this wrapper
    std::shared_ptr<dccl::Codec> codec() { return codec_; }

    void set_cfg(const protobuf::DCCLConfig& cfg)
    {
        cfg_.CopyFrom(cfg);
        process_cfg(true);
    }

    void merge_cfg(const protobuf::DCCLConfig& cfg)
    {
        bool new_id_codec = (cfg_.id_codec() != cfg.id_codec());
        cfg_.MergeFrom(cfg);
        process_cfg(new_id_codec);
    }

    void load_shared_library_codecs(void* dl_handle)
    {
        codec_->load_library(dl_handle);
        loaded_libs_.insert(dl_handle);
    }

    template <typename ProtobufMessage> void validate() { validate(ProtobufMessage::descriptor()); }

    template <typename ProtobufMessage> void info(std::ostream* os) const
    {
        info(ProtobufMessage::descriptor(), os);
    }

    void info_all(std::ostream* os) const { codec_->info_all(os); }

    template <typename ProtobufMessage> unsigned id() const
    {
        return id(ProtobufMessage::descriptor());
    }

    unsigned size(const google::protobuf::Message& msg) { return codec_->size(msg); }

    static const std::string& glog_encode_group() { return glog_encode_group_; }
    static const std::string& glog_decode_group() { return glog_decode_group_; }

    void encode(std::string* bytes, const google::protobuf::Message& msg, bool header_only = false)
    {
        bytes->clear();
        codec_->encode(bytes, msg, header_only);
    }

    void decode(const std::string& bytes, google::protobuf::Message* msg, bool header_only = false)
    {
        codec_->decode(bytes, msg, header_only);
    }

    unsigned id_from_encoded(const std::string& bytes) { return codec_->id(bytes); }

    void validate(const google::protobuf::Descriptor* desc)
    {
        codec_->load(desc);
        loaded_msgs_.insert(desc);
    }

    void validate_repeated(const std::list<const google::protobuf::Descriptor*>& descs)
    {
        for (const google::protobuf::Descriptor* p : descs) validate(p);
    }

    void info(const google::protobuf::Descriptor* desc, std::ostream* os) const
    {
        codec_->info(desc, os);
    }

    void info_repeated(const std::list<const google::protobuf::Descriptor*>& desc,
                       std::ostream* os) const
    {
        for (const google::protobuf::Descriptor* p : desc) info(p, os);
    }

    unsigned id(const google::protobuf::Descriptor* desc) const
    {
        return desc->options().GetExtension(dccl::msg).id();
    }

    template <typename GoogleProtobufMessagePointer>
    unsigned size_repeated(const std::list<GoogleProtobufMessagePointer>& msgs)
    {
        unsigned out = 0;
        for (const GoogleProtobufMessagePointer& msg : msgs) out += size(*msg);
        return out;
    }

    template <typename GoogleProtobufMessagePointer>
    GoogleProtobufMessagePointer decode(const std::string& bytes, bool header_only = false)
    {
        return codec_->decode<GoogleProtobufMessagePointer>(bytes, header_only);
    }

    template <typename GoogleProtobufMessagePointer>
    std::string encode_repeated(const std::list<GoogleProtobufMessagePointer>& msgs)
    {
        std::string out;
        for (const GoogleProtobufMessagePointer& msg : msgs)
        {
            std::string piece;
            encode(&piece, *msg);
            out += piece;
        }

        return out;
    }

    template <typename GoogleProtobufMessagePointer>
    std::list<GoogleProtobufMessagePointer> decode_repeated(const std::string& orig_bytes)
    {
        std::string bytes = orig_bytes;
        std::list<GoogleProtobufMessagePointer> out;
        while (!bytes.empty())
        {
            try
            {
                out.push_back(decode<GoogleProtobufMessagePointer>(bytes));
                unsigned last_size = size(*out.back());
                glog.is(util::logger::DEBUG1) && glog << "last message size was: " << last_size
                                                      << std::endl;
                bytes.erase(0, last_size);
            }
            catch (dccl::Exception& e)
            {
                if (out.empty())
                    throw(e);
                else
                {
                    glog.is(util::logger::WARN) &&
                        glog << "failed to decode " << goby::util::hex_encode(bytes)
                             << " but returning parts already decoded" << std::endl;
                    return out;
                }
            }
        }
        return out;
    }

    template <typename DCCLTypedFieldCodecUint32> void add_id_codec(const std::string& identifier)
    {
        dccl::FieldCodecManager::add<DCCLTypedFieldCodecUint32>(identifier);
    }

    void set_id_codec(const std::string& identifier)
    {
        codec_.reset(new dccl::Codec(identifier));

        for (auto loaded_lib : loaded_libs_) load_shared_library_codecs(loaded_lib);

        for (auto loaded_msg : loaded_msgs_)
        {
            try
            {
                validate(loaded_msg);
            }
            catch (dccl::Exception& e)
            {
                glog.is(util::logger::WARN) &&
                    glog << "Failed to reload " << loaded_msg->full_name()
                         << " after ID codec change: " << e.what() << std::endl;
            }
        }
    }

    void reset_id_codec() { set_id_codec(dccl::Codec::default_id_codec_name()); }

    //@}

  private:
    DCCLCodec();

    ~DCCLCodec() = default;
    DCCLCodec(const DCCLCodec&) = delete;
    DCCLCodec& operator=(const DCCLCodec&) = delete;

    void process_cfg(bool new_id_codec)
    {
        if (cfg_.has_crypto_passphrase())
        {
            std::set<unsigned> skip_crypto_ids;
            for (int i = 0, n = cfg_.skip_crypto_for_id_size(); i < n; ++i)
                skip_crypto_ids.insert(cfg_.skip_crypto_for_id(i));
            codec_->set_crypto_passphrase(cfg_.crypto_passphrase(), skip_crypto_ids);
        }

        if (new_id_codec && cfg_.has_id_codec())
        {
            set_id_codec(cfg_.id_codec());
        }
    }

    void dlog_message(const std::string& msg, dccl::logger::Verbosity vrb, dccl::logger::Group grp)
    {
        std::string glog_group =
            (grp == dccl::logger::DECODE) ? glog_decode_group_ : glog_encode_group_;
        std::string prefix = (grp == dccl::logger::SIZE) ? " {size} " : "";
        auto glog_vrb = goby::util::logger::VERBOSE;
        switch (vrb)
        {
            case dccl::logger::WARN_PLUS:
            case dccl::logger::WARN: glog_vrb = goby::util::logger::WARN; break;
            default:
            case dccl::logger::INFO_PLUS:
            case dccl::logger::INFO: glog_vrb = goby::util::logger::VERBOSE; break;
            case dccl::logger::DEBUG1_PLUS:
            case dccl::logger::DEBUG1: glog_vrb = goby::util::logger::DEBUG1; break;
            case dccl::logger::DEBUG2_PLUS:
            case dccl::logger::DEBUG2: glog_vrb = goby::util::logger::DEBUG2; break;
            case dccl::logger::DEBUG3_PLUS:
            case dccl::logger::DEBUG3: glog_vrb = goby::util::logger::DEBUG3; break;
        }

        goby::glog.is(glog_vrb) && goby::glog << group(glog_group) << prefix << msg << std::endl;
    }

  private:
    static std::string glog_encode_group_;
    static std::string glog_decode_group_;

    protobuf::DCCLConfig cfg_;

    std::shared_ptr<dccl::Codec> codec_;

    std::set<void*> loaded_libs_;
    std::set<const google::protobuf::Descriptor*> loaded_msgs_;
};

inline std::ostream& operator<<(std::ostream& os, const DCCLCodec& codec)
{
    codec.info_all(&os);
    return os;
}
} // namespace acomms
} // namespace goby

#endif
