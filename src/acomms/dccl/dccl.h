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

#ifndef DCCLCOMPAT20131116H
#define DCCLCOMPAT20131116H

#include "goby/acomms/acomms_helpers.h" // for operator<< of google::protobuf::Message
#include "goby/acomms/protobuf/dccl.pb.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"

#include "dccl/codec.h"
#include "dccl/codecs2/field_codec_default.h"
#include "dccl/field_codec_id.h"
#include "dccl/internal/field_codec_message_stack.h"

/// The global namespace for the Goby project.
namespace goby
{
namespace util
{
class FlexOstream;
}

/// Objects pertaining to acoustic communications (acomms)
namespace acomms
{
typedef dccl::Exception DCCLException;
typedef dccl::NullValueException DCCLNullValueException;

typedef dccl::DefaultIdentifierCodec DCCLDefaultIdentifierCodec;
template <typename WireType, typename FieldType = WireType>
class DefaultNumericFieldCodec : public dccl::v2::DefaultNumericFieldCodec<WireType, FieldType>
{
};

typedef dccl::v2::DefaultBoolCodec DCCLDefaultBoolCodec;
typedef dccl::v2::DefaultStringCodec DCCLDefaultStringCodec;
typedef dccl::v2::DefaultBytesCodec DCCLDefaultBytesCodec;
typedef dccl::v2::DefaultEnumCodec DCCLDefaultEnumCodec;

class MessageHandler : public dccl::internal::MessageStack
{
  public:
    MessageHandler(const google::protobuf::FieldDescriptor* field = 0) : MessageStack(field) {}
    typedef dccl::MessagePart MessagePart;
    static const MessagePart HEAD = dccl::HEAD, BODY = dccl::BODY, UNKNOWN = dccl::UNKNOWN;
};

template <typename TimeType> class TimeCodec : public dccl::v2::TimeCodecBase<TimeType, 0>
{
    BOOST_STATIC_ASSERT(sizeof(TimeCodec) == 0);
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

typedef dccl::v2::DefaultMessageCodec DCCLDefaultMessageCodec;

typedef dccl::FieldCodecBase DCCLFieldCodecBase;

template <typename WireType, typename FieldType = WireType>
struct DCCLTypedFieldCodec : public dccl::TypedFieldCodec<WireType, FieldType>
{
    typedef dccl::FieldCodecBase DCCLFieldCodecBase;
};

template <typename WireType, typename FieldType = WireType>
struct DCCLTypedFixedFieldCodec : public dccl::TypedFixedFieldCodec<WireType, FieldType>
{
    typedef dccl::FieldCodecBase DCCLFieldCodecBase;
};

template <typename WireType, typename FieldType = WireType>
struct DCCLRepeatedTypedFieldCodec : public dccl::RepeatedTypedFieldCodec<WireType, FieldType>
{
    typedef dccl::FieldCodecBase DCCLFieldCodecBase;
};

typedef dccl::FieldCodecManager DCCLFieldCodecManager;
//        typedef dccl::TypedFieldCodec DCCLTypedFieldCodec;
typedef dccl::FieldCodecManager DCCLFieldCodecManager;
typedef dccl::internal::FromProtoCppTypeBase FromProtoCppTypeBase;
//       typedef dccl::FromProtoType FromProtoType;
// typedef dccl::FromProtoCppType FromProtoCppType;
//typedef dccl::ToProtoCppType ToProtoCppType;
typedef dccl::Bitset Bitset;
typedef dccl::internal::TypeHelper DCCLTypeHelper;

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
                glog.is(common::logger::DEBUG1) && glog << "last message size was: " << last_size
                                                        << std::endl;
                bytes.erase(0, last_size);
            }
            catch (dccl::Exception& e)
            {
                if (out.empty())
                    throw(e);
                else
                {
                    glog.is(common::logger::WARN) &&
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

        for (std::set<void*>::const_iterator it = loaded_libs_.begin(), end = loaded_libs_.end();
             it != end; ++it)
            load_shared_library_codecs(*it);

        for (std::set<const google::protobuf::Descriptor*>::const_iterator
                 it = loaded_msgs_.begin(),
                 end = loaded_msgs_.end();
             it != end; ++it)
        {
            try
            {
                validate(*it);
            }
            catch (dccl::Exception& e)
            {
                glog.is(common::logger::WARN) && glog << "Failed to reload " << (*it)->full_name()
                                                      << " after ID codec change: " << e.what()
                                                      << std::endl;
            }
        }
    }

    void reset_id_codec() { set_id_codec(dccl::Codec::default_id_codec_name()); }

    //@}

  private:
    // so we can use shared_ptr to hold the singleton
    template <typename T> friend void boost::checked_delete(T*);

    DCCLCodec();

    ~DCCLCodec() {}
    DCCLCodec(const DCCLCodec&);
    DCCLCodec& operator=(const DCCLCodec&);

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
        if (grp == dccl::logger::DECODE)
            goby::glog << group(glog_decode_group_) << msg << std::endl;
        else if (grp == dccl::logger::ENCODE)
            goby::glog << group(glog_encode_group_) << msg << std::endl;
        else if (grp == dccl::logger::SIZE)
            goby::glog << group(glog_encode_group_) << " {size} " << msg << std::endl;
        else
            goby::glog << group(glog_encode_group_) << msg << std::endl;
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
