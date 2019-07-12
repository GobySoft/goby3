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

#ifndef MOOSPROTOBUFHELPERS20110216H
#define MOOSPROTOBUFHELPERS20110216H

#include <limits>
#include <regex>

#include <boost/format.hpp>

#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/tokenizer.h>

#include "dccl/dynamic_protobuf_manager.h"

#include "goby/moos/moos_string.h"
#include "goby/moos/protobuf/goby_moos_app.pb.h"
#include "goby/moos/protobuf/translator.pb.h"
#include "goby/moos/transitional/message_algorithms.h"
#include "goby/moos/transitional/message_val.h"
#include "goby/util/as.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/primitive_types.h"

namespace goby
{
namespace moos
{
const std::string MAGIC_PROTOBUF_HEADER = "@PB";

inline std::map<int, std::string>
run_serialize_algorithms(const google::protobuf::Message& in,
                         const google::protobuf::RepeatedPtrField<
                             protobuf::TranslatorEntry::PublishSerializer::Algorithm>& algorithms)
{
    const google::protobuf::Descriptor* desc = in.GetDescriptor();

    // run algorithms
    typedef google::protobuf::RepeatedPtrField<
        protobuf::TranslatorEntry::PublishSerializer::Algorithm>::const_iterator const_iterator;

    std::map<int, std::string> modified_values;

    for (const_iterator it = algorithms.begin(), n = algorithms.end(); it != n; ++it)
    {
        const google::protobuf::FieldDescriptor* primary_field_desc =
            desc->FindFieldByNumber(it->primary_field());

        if (!primary_field_desc || primary_field_desc->is_repeated())
            continue;

        std::string primary_val;

        if (!modified_values.count(it->output_virtual_field()))
        {
            google::protobuf::TextFormat::PrintFieldValueToString(in, primary_field_desc, -1,
                                                                  &primary_val);
            boost::trim_if(primary_val, boost::is_any_of("\""));
        }
        else
        {
            primary_val = modified_values[it->output_virtual_field()];
        }

        goby::moos::transitional::DCCLMessageVal val(primary_val);
        std::vector<goby::moos::transitional::DCCLMessageVal> ref;
        for (int i = 0, m = it->reference_field_size(); i < m; ++i)
        {
            const google::protobuf::FieldDescriptor* field_desc =
                desc->FindFieldByNumber(it->reference_field(i));

            if (field_desc && !field_desc->is_repeated())
            {
                std::string ref_value;
                google::protobuf::TextFormat::PrintFieldValueToString(in, field_desc, -1,
                                                                      &ref_value);
                ref.push_back(ref_value);
            }
            else
            {
                throw(std::runtime_error("Reference field given is invalid or repeated (must be "
                                         "optional or required): " +
                                         goby::util::as<std::string>(it->reference_field(i))));
            }
        }

        moos::transitional::DCCLAlgorithmPerformer::getInstance()->run_algorithm(it->name(), val,
                                                                                 ref);

        val = std::string(val);
        modified_values[it->output_virtual_field()] = std::string(val);
    }

    return modified_values;
}

inline std::string strip_name_from_enum(const std::string& enum_value,
                                        const std::string& field_name)
{
    return boost::ierase_first_copy(enum_value, field_name + "_");
}

inline std::string add_name_to_enum(const std::string& enum_value, const std::string& field_name)
{
    return boost::to_upper_copy(field_name) + "_" + enum_value;
}

template <goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique technique>
class MOOSTranslation
{
};

template <> class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT>
{
  public:
    static void serialize(std::string* out, const google::protobuf::Message& in)
    {
        google::protobuf::TextFormat::Printer printer;
        printer.SetSingleLineMode(true);
        printer.PrintToString(in, out);
    }

    static void parse(const std::string& in, google::protobuf::Message* out)
    {
        google::protobuf::TextFormat::Parser parser;
        goby::util::FlexOStreamErrorCollector error_collector(in);
        parser.RecordErrorsTo(&error_collector);
        parser.ParseFromString(in, out);
    }
};

template <> class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED>
{
  public:
    static void serialize(std::string* out, const google::protobuf::Message& in)
    {
        in.SerializeToString(out);
    }

    static void parse(const std::string& in, google::protobuf::Message* out)
    {
        out->ParseFromString(in);
    }
};

template <> class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX>
{
  public:
    static void serialize(std::string* out, const google::protobuf::Message& in)
    {
        std::string native_encoded;
        goby::moos::MOOSTranslation<
            goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::
            serialize(&native_encoded, in);
        goby::util::hex_encode(native_encoded, out);
    }

    static void parse(const std::string& in, google::protobuf::Message* out)
    {
        std::string native_encoded;
        goby::util::hex_decode(in, &native_encoded);
        goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                        TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::parse(native_encoded,
                                                                                  out);
    }
};

template <goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique prefix_technique,
          goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique base_technique>
class MOOSPrefixTranslation
{
  public:
    static void serialize(std::string* out, const google::protobuf::Message& msg)
    {
        std::string header =
            goby::moos::MAGIC_PROTOBUF_HEADER + "[" + msg.GetDescriptor()->full_name() + "] ";
        goby::moos::MOOSTranslation<base_technique>::serialize(out, msg);
        *out = header + *out;
    }

    static void parse(const std::string& in, google::protobuf::Message* msg)
    {
        if (in.size() > goby::moos::MAGIC_PROTOBUF_HEADER.size() &&
            in.substr(0, goby::moos::MAGIC_PROTOBUF_HEADER.size()) ==
                goby::moos::MAGIC_PROTOBUF_HEADER)
        {
            size_t end_bracket_pos = in.find(']');

            if (end_bracket_pos == std::string::npos)
                throw(std::runtime_error(
                    "Incorrectly formatted protobuf message passed to MOOSTranslation<" +
                    goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                        prefix_technique) +
                    ">::parse."));

            std::string name =
                in.substr(goby::moos::MAGIC_PROTOBUF_HEADER.size() + 1,
                          end_bracket_pos - 1 - goby::moos::MAGIC_PROTOBUF_HEADER.size());
            if (name != msg->GetDescriptor()->full_name())
                throw(std::runtime_error(
                    "Wrong Protobuf type passed to MOOSTranslation<" +
                    goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                        prefix_technique) +
                    ">::parse. Expected: " + msg->GetDescriptor()->full_name() +
                    ", received: " + name));

            if (in.size() > end_bracket_pos + 2)
                goby::moos::MOOSTranslation<base_technique>::parse(in.substr(end_bracket_pos + 2),
                                                                   msg);
            else
                msg->Clear();
        }
        else
        {
            goby::moos::MOOSTranslation<base_technique>::parse(in, msg);
        }
    }

    static std::shared_ptr<google::protobuf::Message> dynamic_parse(const std::string& in)
    {
        if (in.size() > goby::moos::MAGIC_PROTOBUF_HEADER.size() &&
            in.substr(0, goby::moos::MAGIC_PROTOBUF_HEADER.size()) ==
                goby::moos::MAGIC_PROTOBUF_HEADER)
        {
            size_t end_bracket_pos = in.find(']');

            if (end_bracket_pos == std::string::npos)
                throw(std::runtime_error(
                    "Incorrectly formatted protobuf message passed to MOOSTranslation<" +
                    goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                        prefix_technique) +
                    ">::dynamic_parse."));

            std::string name =
                in.substr(goby::moos::MAGIC_PROTOBUF_HEADER.size() + 1,
                          end_bracket_pos - 1 - goby::moos::MAGIC_PROTOBUF_HEADER.size());

            try
            {
                auto return_message = dccl::DynamicProtobufManager::new_protobuf_message<
                    std::shared_ptr<google::protobuf::Message>>(name);
                if (in.size() > end_bracket_pos + 1)
                    goby::moos::MOOSTranslation<base_technique>::parse(
                        in.substr(end_bracket_pos + 1), return_message.get());
                return return_message;
            }
            catch (std::exception& e)
            {
                return std::shared_ptr<google::protobuf::Message>();
            }
        }
        else
        {
            return std::shared_ptr<google::protobuf::Message>();
        }
    }
};

template <>
class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>
    : public MOOSPrefixTranslation<
          protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT,
          protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT>
{
};
template <>
class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>
    : public MOOSPrefixTranslation<
          protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED,
          protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED>
{
};
template <>
class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>
    : public MOOSPrefixTranslation<
          protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX,
          protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX>
{
};

template <>
class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS>
{
  public:
    static void serialize(std::string* out, const google::protobuf::Message& in,
                          const google::protobuf::RepeatedPtrField<
                              protobuf::TranslatorEntry::PublishSerializer::Algorithm>& algorithms =
                              google::protobuf::RepeatedPtrField<
                                  protobuf::TranslatorEntry::PublishSerializer::Algorithm>(),
                          bool use_short_enum = false)
    {
        std::stringstream out_ss;

        const google::protobuf::Descriptor* desc = in.GetDescriptor();
        const google::protobuf::Reflection* refl = in.GetReflection();

        int included_fields = 0;
        for (int i = 0, n = desc->field_count(); i < n; ++i)
        {
            const google::protobuf::FieldDescriptor* field_desc = desc->field(i);

            // leave out unspecified or empty fields
            if ((!field_desc->is_repeated() && !refl->HasField(in, field_desc)) ||
                (field_desc->is_repeated() && refl->FieldSize(in, field_desc) == 0))
                continue;

            if (included_fields)
                out_ss << ",";
            ++included_fields;

            const std::string& field_name = field_desc->name();

            switch (field_desc->cpp_type())
            {
                default:
                    out_ss << to_moos_comma_equals_string_field(in, field_desc, true,
                                                                use_short_enum);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                    if (field_desc->is_repeated())
                    {
                        for (int k = 0, o = field_desc->message_type()->field_count(); k < o; ++k)
                        {
                            if (k)
                                out_ss << ",";
                            out_ss << field_name << "_"
                                   << field_desc->message_type()->field(k)->name() << "={";
                            for (int j = 0, m = refl->FieldSize(in, field_desc); j < m; ++j)
                            {
                                if (j)
                                    out_ss << ",";
                                const google::protobuf::Message& embedded_msg =
                                    refl->GetRepeatedMessage(in, field_desc, j);
                                out_ss << to_moos_comma_equals_string_field(
                                    embedded_msg, embedded_msg.GetDescriptor()->field(k), false,
                                    use_short_enum);
                            }
                            out_ss << "}";
                        }
                    }

                    else
                    {
                        for (int k = 0, o = field_desc->message_type()->field_count(); k < o; ++k)
                        {
                            if (k)
                                out_ss << ",";
                            out_ss << field_name << "_"
                                   << field_desc->message_type()->field(k)->name() << "=";
                            const google::protobuf::Message& embedded_msg =
                                refl->GetMessage(in, field_desc);
                            out_ss << to_moos_comma_equals_string_field(
                                embedded_msg, embedded_msg.GetDescriptor()->field(k), false,
                                use_short_enum);
                        }
                    }

                    break;
            }
        }

        std::map<int, std::string> additional_values = run_serialize_algorithms(in, algorithms);
        for (std::map<int, std::string>::const_iterator it = additional_values.begin(),
                                                        n = additional_values.end();
             it != n; ++it)
        {
            out_ss << ",";

            std::string key;

            typedef google::protobuf::RepeatedPtrField<
                protobuf::TranslatorEntry::PublishSerializer::Algorithm>::const_iterator
                const_iterator;

            int primary_field;
            for (const_iterator alg_it = algorithms.begin(), alg_n = algorithms.end();
                 alg_it != alg_n; ++alg_it)
            {
                if (alg_it->output_virtual_field() == it->first)
                {
                    if (!key.empty())
                        key += "+";

                    key += alg_it->name();
                    primary_field = alg_it->primary_field();
                }
            }
            key += "(" + desc->FindFieldByNumber(primary_field)->name() + ")";

            out_ss << key << "=" << it->second;
        }

        *out = out_ss.str();
    }

    static void parse(std::string in, google::protobuf::Message* out,
                      const google::protobuf::RepeatedPtrField<
                          protobuf::TranslatorEntry::CreateParser::Algorithm>& algorithms =
                          google::protobuf::RepeatedPtrField<
                              protobuf::TranslatorEntry::CreateParser::Algorithm>(),
                      bool use_short_enum = false)
    {
        const google::protobuf::Descriptor* desc = out->GetDescriptor();
        const google::protobuf::Reflection* refl = out->GetReflection();

        boost::to_lower(in);

        for (int i = 0, n = desc->field_count(); i < n; ++i)
        {
            const google::protobuf::FieldDescriptor* field_desc = desc->field(i);

            switch (field_desc->cpp_type())
            {
                default:
                {
                    std::string val;

                    if (goby::moos::val_from_string(val, in, field_desc->name()))
                    {
                        // run algorithms
                        typedef google::protobuf::RepeatedPtrField<
                            protobuf::TranslatorEntry::CreateParser::Algorithm>::const_iterator
                            const_iterator;

                        for (const_iterator it = algorithms.begin(), n = algorithms.end(); it != n;
                             ++it)
                        {
                            goby::moos::transitional::DCCLMessageVal extract_val(val);

                            if (it->primary_field() == field_desc->number())
                                moos::transitional::DCCLAlgorithmPerformer::getInstance()
                                    ->run_algorithm(
                                        it->name(), extract_val,
                                        std::vector<goby::moos::transitional::DCCLMessageVal>());

                            val = std::string(extract_val);
                        }

                        std::vector<std::string> vals;
                        boost::split(vals, val, boost::is_any_of(","));
                        from_moos_comma_equals_string_field(out, field_desc, vals, 0,
                                                            use_short_enum);
                    }
                }
                break;

                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                    if (field_desc->is_repeated())
                    {
                        for (int k = 0, o = field_desc->message_type()->field_count(); k < o; ++k)
                        {
                            std::string val;
                            if (goby::moos::val_from_string(
                                    val, in,
                                    field_desc->name() + "_" +
                                        field_desc->message_type()->field(k)->name()))
                            {
                                std::vector<std::string> vals;
                                boost::split(vals, val, boost::is_any_of(","));

                                for (int j = 0, m = vals.size(); j < m; ++j)
                                {
                                    google::protobuf::Message* embedded_msg =
                                        (refl->FieldSize(*out, field_desc) < j + 1)
                                            ? refl->AddMessage(out, field_desc)
                                            : refl->MutableRepeatedMessage(out, field_desc, j);
                                    from_moos_comma_equals_string_field(
                                        embedded_msg, embedded_msg->GetDescriptor()->field(k), vals,
                                        j, use_short_enum);
                                }
                            }
                        }
                    }
                    else
                    {
                        for (int k = 0, o = field_desc->message_type()->field_count(); k < o; ++k)
                        {
                            std::string val;
                            if (goby::moos::val_from_string(
                                    val, in,
                                    field_desc->name() + "_" +
                                        field_desc->message_type()->field(k)->name()))
                            {
                                std::vector<std::string> vals;
                                boost::split(vals, val, boost::is_any_of(","));

                                google::protobuf::Message* embedded_msg =
                                    refl->MutableMessage(out, field_desc);
                                from_moos_comma_equals_string_field(
                                    embedded_msg, embedded_msg->GetDescriptor()->field(k), vals, 0,
                                    use_short_enum);
                            }
                        }
                    }
                    break;
            }
        }
    }

  private:
    static std::string
    to_moos_comma_equals_string_field(const google::protobuf::Message& proto_msg,
                                      const google::protobuf::FieldDescriptor* field_desc,
                                      bool write_key = true, bool use_short_enum = false)
    {
        const google::protobuf::Reflection* refl = proto_msg.GetReflection();

        std::stringstream out;
        const std::string& field_name = field_desc->name();

        if (field_desc->is_repeated())
        {
            if (write_key)
                out << field_name << "={";

            for (int j = 0, m = refl->FieldSize(proto_msg, field_desc); j < m; ++j)
            {
                if (j)
                    out << ",";

                switch (field_desc->cpp_type())
                {
                    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                        out << goby::util::hex_encode(
                            refl->GetRepeatedMessage(proto_msg, field_desc, j).SerializeAsString());
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                        out << refl->GetRepeatedInt32(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                        out << refl->GetRepeatedInt64(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                        out << refl->GetRepeatedUInt32(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                        out << refl->GetRepeatedUInt64(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                        out << std::boolalpha << refl->GetRepeatedBool(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                        if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
                            out << refl->GetRepeatedString(proto_msg, field_desc, j);
                        else if (field_desc->type() ==
                                 google::protobuf::FieldDescriptor::TYPE_BYTES)
                            out << goby::util::hex_encode(
                                refl->GetRepeatedString(proto_msg, field_desc, j));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                        out << std::setprecision(std::numeric_limits<float>::digits10)
                            << refl->GetRepeatedFloat(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                        out << std::setprecision(std::numeric_limits<double>::digits10)
                            << refl->GetRepeatedDouble(proto_msg, field_desc, j);
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                        out << ((use_short_enum)
                                    ? strip_name_from_enum(
                                          refl->GetRepeatedEnum(proto_msg, field_desc, j)->name(),
                                          field_name)
                                    : refl->GetRepeatedEnum(proto_msg, field_desc, j)->name());

                        break;
                }
            }
            if (write_key)
                out << "}";
        }
        else
        {
            if (write_key)
                out << field_name << "=";
            switch (field_desc->cpp_type())
            {
                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                    out << goby::util::hex_encode(
                        refl->GetMessage(proto_msg, field_desc).SerializeAsString());
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                    out << refl->GetInt32(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                    out << refl->GetInt64(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                    out << refl->GetUInt32(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                    out << refl->GetUInt64(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                    out << std::boolalpha << refl->GetBool(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                    if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
                        out << refl->GetString(proto_msg, field_desc);
                    else if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
                        out << goby::util::hex_encode(refl->GetString(proto_msg, field_desc));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                    out << std::setprecision(std::numeric_limits<float>::digits10)
                        << refl->GetFloat(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                    out << std::setprecision(std::numeric_limits<double>::digits10)
                        << refl->GetDouble(proto_msg, field_desc);
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                    out << ((use_short_enum)
                                ? strip_name_from_enum(refl->GetEnum(proto_msg, field_desc)->name(),
                                                       field_name)
                                : refl->GetEnum(proto_msg, field_desc)->name());
                    break;
            }
        }
        return out.str();
    }
    static void from_moos_comma_equals_string_field(
        google::protobuf::Message* proto_msg, const google::protobuf::FieldDescriptor* field_desc,
        const std::vector<std::string>& values, int value_key = 0, bool use_short_enum = false)
    {
        if (values.size() == 0)
            return;

        const google::protobuf::Reflection* refl = proto_msg->GetReflection();
        if (field_desc->is_repeated())
        {
            for (int j = 0, m = values.size(); j < m; ++j)
            {
                const std::string& v = values[j];

                switch (field_desc->cpp_type())
                {
                    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                        refl->AddMessage(proto_msg, field_desc)
                            ->ParseFromString(goby::util::hex_decode(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                        refl->AddInt32(proto_msg, field_desc,
                                       goby::util::as<google::protobuf::int32>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                        refl->AddInt64(proto_msg, field_desc,
                                       goby::util::as<google::protobuf::int64>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                        refl->AddUInt32(proto_msg, field_desc,
                                        goby::util::as<google::protobuf::uint32>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                        refl->AddUInt64(proto_msg, field_desc,
                                        goby::util::as<google::protobuf::uint64>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                        refl->AddBool(proto_msg, field_desc, goby::util::as<bool>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                        if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
                            refl->AddString(proto_msg, field_desc, v);
                        else if (field_desc->type() ==
                                 google::protobuf::FieldDescriptor::TYPE_BYTES)
                            refl->AddString(proto_msg, field_desc, goby::util::hex_decode(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                        refl->AddFloat(proto_msg, field_desc, goby::util::as<float>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                        refl->AddDouble(proto_msg, field_desc, goby::util::as<double>(v));
                        break;

                    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                    {
                        std::string enum_value =
                            ((use_short_enum) ? add_name_to_enum(v, field_desc->name()) : v);

                        const google::protobuf::EnumValueDescriptor* enum_desc =
                            field_desc->enum_type()->FindValueByName(enum_value);

                        // try upper case
                        if (!enum_desc)
                            enum_desc = field_desc->enum_type()->FindValueByName(
                                boost::algorithm::to_upper_copy(enum_value));
                        // try lower case
                        if (!enum_desc)
                            enum_desc = field_desc->enum_type()->FindValueByName(
                                boost::algorithm::to_lower_copy(enum_value));

                        if (enum_desc)
                            refl->AddEnum(proto_msg, field_desc, enum_desc);
                    }
                    break;
                }
            }
        }
        else
        {
            const std::string& v = values[value_key];
            switch (field_desc->cpp_type())
            {
                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                    refl->MutableMessage(proto_msg, field_desc)
                        ->ParseFromString(goby::util::hex_decode(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                    refl->SetInt32(proto_msg, field_desc,
                                   goby::util::as<google::protobuf::int32>(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                    refl->SetInt64(proto_msg, field_desc,
                                   goby::util::as<google::protobuf::int64>(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                    refl->SetUInt32(proto_msg, field_desc,
                                    goby::util::as<google::protobuf::uint32>(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                    refl->SetUInt64(proto_msg, field_desc,
                                    goby::util::as<google::protobuf::uint64>(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                    refl->SetBool(proto_msg, field_desc, goby::util::as<bool>(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                    if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
                        refl->SetString(proto_msg, field_desc, v);
                    else if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
                        refl->SetString(proto_msg, field_desc, goby::util::hex_decode(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                    refl->SetFloat(proto_msg, field_desc, goby::util::as<float>(v));
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                    refl->SetDouble(proto_msg, field_desc, goby::util::as<double>(v)); /*  */
                    break;

                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                {
                    std::string enum_value =
                        ((use_short_enum) ? add_name_to_enum(v, field_desc->name()) : v);

                    const google::protobuf::EnumValueDescriptor* enum_desc =
                        field_desc->enum_type()->FindValueByName(enum_value);

                    // try upper case
                    if (!enum_desc)
                        enum_desc = field_desc->enum_type()->FindValueByName(
                            boost::algorithm::to_upper_copy(enum_value));
                    // try lower case
                    if (!enum_desc)
                        enum_desc = field_desc->enum_type()->FindValueByName(
                            boost::algorithm::to_lower_copy(enum_value));

                    if (enum_desc)
                        refl->SetEnum(proto_msg, field_desc, enum_desc);
                }
                break;
            }
        }
    }
};

template <> class MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_FORMAT>
{
  public:
    struct RepeatedFieldKey
    {
        int field;
        int index;
    };

    static void serialize(std::string* out, const google::protobuf::Message& in,
                          const google::protobuf::RepeatedPtrField<
                              protobuf::TranslatorEntry::PublishSerializer::Algorithm>& algorithms,
                          const std::string& format, const std::string& repeated_delimiter,
                          bool use_short_enum = false)
    {
        std::string mutable_format = format;

        const google::protobuf::Descriptor* desc = in.GetDescriptor();
        const google::protobuf::Reflection* refl = in.GetReflection();

        int max_field_number = 1;
        for (int i = 1, n = desc->field_count(); i < n; ++i)
        {
            const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
            if (field_desc->number() > max_field_number)
                max_field_number = field_desc->number();
        }

        // run algorithms
        std::map<int, std::string> modified_values = run_serialize_algorithms(in, algorithms);

        for (std::map<int, std::string>::const_iterator it = modified_values.begin(),
                                                        n = modified_values.end();
             it != n; ++it)
        {
            if (it->first > max_field_number)
                max_field_number = it->first;
        }

        std::string mutable_format_temp = mutable_format;

        std::regex moos_index_regex("%([0-9\\.]+:)+[0-9\\.]+%");
        for (std::sregex_iterator
                 it(mutable_format.begin(), mutable_format.end(), moos_index_regex),
             end;
             it != end; ++it)
        {
            std::string match = (*it)[0];

            boost::trim_if(match, boost::is_any_of("%"));
            std::vector<std::string> subfields;
            boost::split(subfields, match, boost::is_any_of(":"));

            ++max_field_number;

            const google::protobuf::FieldDescriptor* field_desc = 0;
            const google::protobuf::Reflection* sub_refl = refl;
            const google::protobuf::Message* sub_message = &in;

            for (int i = 0, n = subfields.size() - 1; i < n; ++i)
            {
                std::vector<std::string> field_and_index;
                boost::split(field_and_index, subfields[i], boost::is_any_of("."));

                field_desc = sub_message->GetDescriptor()->FindFieldByNumber(
                    goby::util::as<int>(field_and_index[0]));
                if (!field_desc ||
                    field_desc->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
                {
                    throw(std::runtime_error(
                        "Invalid ':' syntax given for format: " + match +
                        ". All field indices except the last must be embedded messages"));
                }
                if (field_desc->is_repeated() && field_and_index.size() != 2)
                {
                    throw(std::runtime_error("Invalid '.' syntax given for format: " + match +
                                             ". Repeated message, but no valid index given. E.g., "
                                             "use '3.4' for index 4 of field 3."));
                }

                sub_message =
                    (field_desc->is_repeated())
                        ? &sub_refl->GetRepeatedMessage(*sub_message, field_desc,
                                                        goby::util::as<int>(field_and_index[1]))
                        : &sub_refl->GetMessage(*sub_message, field_desc);
                sub_refl = sub_message->GetReflection();
            }

            serialize(&modified_values[max_field_number], *sub_message, algorithms,
                      "%" + subfields[subfields.size() - 1] + "%", repeated_delimiter,
                      use_short_enum);

            boost::replace_all(
                mutable_format_temp, std::string("%" + match + "%"),
                std::string("%" + goby::util::as<std::string>(max_field_number) + "%"));
        }

        mutable_format = mutable_format_temp;

        std::map<int, RepeatedFieldKey> indexed_repeated_fields;

        std::regex repeated_field_regex("%[0-9]+\\.[0-9]+%");
        for (std::sregex_iterator
                 it(mutable_format.begin(), mutable_format.end(), repeated_field_regex),
             end;
             it != end; ++it)
        {
            std::string match = (*it)[0];
            boost::trim_if(match, boost::is_any_of("%"));

            ++max_field_number;

            boost::replace_all(
                mutable_format_temp, std::string("%" + match + "%"),
                std::string("%" + goby::util::as<std::string>(max_field_number) + "%"));

            RepeatedFieldKey key;

            std::vector<std::string> field_and_index;
            boost::split(field_and_index, match, boost::is_any_of("."));

            key.field = goby::util::as<int>(field_and_index[0]);
            key.index = goby::util::as<int>(field_and_index[1]);

            indexed_repeated_fields[max_field_number] = key;
        }

        mutable_format = mutable_format_temp;

        boost::format out_format(mutable_format);
        out_format.exceptions(boost::io::all_error_bits ^
                              (boost::io::too_many_args_bit | boost::io::too_few_args_bit));

        for (int i = 1; i <= max_field_number; ++i)
        {
            bool is_indexed_repeated_field = indexed_repeated_fields.count(i);

            const google::protobuf::FieldDescriptor* field_desc = desc->FindFieldByNumber(
                is_indexed_repeated_field ? indexed_repeated_fields[i].field : i);
            std::map<int, std::string>::const_iterator mod_it = modified_values.find(i);
            if (field_desc)
            {
                if (field_desc->is_repeated())
                {
                    int start = (is_indexed_repeated_field) ? indexed_repeated_fields[i].index : 0;
                    int end = (is_indexed_repeated_field) ? indexed_repeated_fields[i].index + 1
                                                          : refl->FieldSize(in, field_desc);

                    std::stringstream out_repeated;
                    for (int j = start; j < end; ++j)
                    {
                        if (j && !is_indexed_repeated_field)
                            out_repeated << repeated_delimiter;
                        switch (field_desc->cpp_type())
                        {
                            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                                out_repeated << goby::util::hex_encode(
                                    refl->GetRepeatedMessage(in, field_desc, j)
                                        .SerializeAsString());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                                out_repeated << ((j < refl->FieldSize(in, field_desc))
                                                     ? refl->GetRepeatedInt32(in, field_desc, j)
                                                     : std::numeric_limits<std::int32_t>::max());

                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                                out_repeated << ((j < refl->FieldSize(in, field_desc))
                                                     ? refl->GetRepeatedInt64(in, field_desc, j)
                                                     : std::numeric_limits<std::int64_t>::max());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                                out_repeated << ((j < refl->FieldSize(in, field_desc))
                                                     ? refl->GetRepeatedUInt32(in, field_desc, j)
                                                     : std::numeric_limits<std::uint32_t>::max());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                                out_repeated << ((j < refl->FieldSize(in, field_desc))
                                                     ? refl->GetRepeatedUInt64(in, field_desc, j)
                                                     : std::numeric_limits<std::uint64_t>::max());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                                out_repeated << std::boolalpha
                                             << ((j < refl->FieldSize(in, field_desc))
                                                     ? refl->GetRepeatedBool(in, field_desc, j)
                                                     : field_desc->default_value_bool());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                                if (field_desc->type() ==
                                    google::protobuf::FieldDescriptor::TYPE_STRING)
                                    out_repeated
                                        << ((j < refl->FieldSize(in, field_desc))
                                                ? refl->GetRepeatedString(in, field_desc, j)
                                                : field_desc->default_value_string());
                                else if (field_desc->type() ==
                                         google::protobuf::FieldDescriptor::TYPE_BYTES)
                                    out_repeated << goby::util::hex_encode(
                                        ((j < refl->FieldSize(in, field_desc))
                                             ? refl->GetRepeatedString(in, field_desc, j)
                                             : field_desc->default_value_string()));
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                                out_repeated
                                    << std::setprecision(std::numeric_limits<float>::digits10)
                                    << ((j < refl->FieldSize(in, field_desc))
                                            ? refl->GetRepeatedFloat(in, field_desc, j)
                                            : std::numeric_limits<float>::quiet_NaN());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                                out_repeated
                                    << std::setprecision(std::numeric_limits<double>::digits10)
                                    << ((j < refl->FieldSize(in, field_desc))
                                            ? refl->GetRepeatedDouble(in, field_desc, j)
                                            : std::numeric_limits<double>::quiet_NaN());
                                break;

                            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                            {
                                const google::protobuf::EnumValueDescriptor* enum_val =
                                    ((j < refl->FieldSize(in, field_desc))
                                         ? refl->GetRepeatedEnum(in, field_desc, j)
                                         : field_desc->default_value_enum());
                                out_repeated
                                    << ((use_short_enum) ? strip_name_from_enum(enum_val->name(),
                                                                                field_desc->name())
                                                         : enum_val->name());
                            }
                            break;
                        }
                    }
                    out_format % out_repeated.str();
                }
                else
                {
                    switch (field_desc->cpp_type())
                    {
                        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                            out_format % goby::util::hex_encode(
                                             refl->GetMessage(in, field_desc).SerializeAsString());
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                            out_format % refl->GetInt32(in, field_desc);
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                            out_format % refl->GetInt64(in, field_desc);
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                            out_format % refl->GetUInt32(in, field_desc);
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                            out_format % refl->GetUInt64(in, field_desc);
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                            out_format % goby::util::as<std::string>(refl->GetBool(in, field_desc));
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                            if (field_desc->type() ==
                                google::protobuf::FieldDescriptor::TYPE_STRING)
                                out_format % refl->GetString(in, field_desc);
                            else if (field_desc->type() ==
                                     google::protobuf::FieldDescriptor::TYPE_BYTES)
                                out_format %
                                    goby::util::hex_encode(refl->GetString(in, field_desc));
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                            out_format % boost::io::group(std::setprecision(
                                                              std::numeric_limits<float>::digits10),
                                                          refl->GetFloat(in, field_desc));
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                            out_format %
                                boost::io::group(
                                    std::setprecision(std::numeric_limits<double>::digits10),
                                    refl->GetDouble(in, field_desc));
                            break;

                        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:

                            out_format %
                                ((use_short_enum)
                                     ? strip_name_from_enum(refl->GetEnum(in, field_desc)->name(),
                                                            field_desc->name())
                                     : refl->GetEnum(in, field_desc)->name());
                            break;
                    }
                }
            }
            else if (mod_it != modified_values.end())
            {
                out_format % mod_it->second;
            }
            else
            {
                out_format % "unknown";
            }
        }

        *out = out_format.str();
    }

    static void parse(const std::string& in, google::protobuf::Message* out, std::string format,
                      const std::string& repeated_delimiter,
                      const google::protobuf::RepeatedPtrField<
                          protobuf::TranslatorEntry::CreateParser::Algorithm>& algorithms =
                          google::protobuf::RepeatedPtrField<
                              protobuf::TranslatorEntry::CreateParser::Algorithm>(),
                      bool use_short_enum = false)
    {
        const google::protobuf::Descriptor* desc = out->GetDescriptor();
        const google::protobuf::Reflection* refl = out->GetReflection();
        boost::to_lower(format);
        std::string str = in;
        std::string lower_str = boost::to_lower_copy(in);

        //    goby::glog.is(DEBUG1) && goby::glog << "Format: " << format << std::endl;
        //   goby::glog.is(DEBUG1) && goby::glog << "String: " << str << std::endl;
        //    goby::glog.is(DEBUG1) && goby::glog << "Lower String: " << lower_str << std::endl;

        std::string::const_iterator i = format.begin();

        while (i != format.end())
        {
            if (*i == '%')
            {
                ++i; // now *i is the conversion specifier
                std::string specifier;
                while (*i != '%') specifier += *i++;

                ++i; // now *i is the next separator
                std::string extract = str.substr(0, lower_str.find(*i));

                if (specifier.find(":") != std::string::npos)
                {
                    std::vector<std::string> subfields;
                    boost::split(subfields, specifier, boost::is_any_of(":"));
                    const google::protobuf::FieldDescriptor* field_desc = 0;
                    const google::protobuf::Reflection* sub_refl = refl;
                    google::protobuf::Message* sub_message = out;

                    for (int i = 0, n = subfields.size() - 1; i < n; ++i)
                    {
                        std::vector<std::string> field_and_index;
                        boost::split(field_and_index, subfields[i], boost::is_any_of("."));

                        field_desc = sub_message->GetDescriptor()->FindFieldByNumber(
                            goby::util::as<int>(field_and_index[0]));
                        if (!field_desc || field_desc->cpp_type() !=
                                               google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
                        {
                            throw(std::runtime_error(
                                "Invalid ':' syntax given for format: " + specifier +
                                ". All field indices except the last must be singular embedded "
                                "messages"));
                        }

                        int index = -1;
                        if (field_desc->is_repeated())
                        {
                            if (field_and_index.size() != 2)
                                throw(std::runtime_error(
                                    "Invalid '.' syntax given for format: " + specifier +
                                    ". Repeated message, but no valid index given. E.g., use '3.4' "
                                    "for index 4 of field 3."));
                            index = goby::util::as<int>(field_and_index.at(1));
                            while (sub_refl->FieldSize(*sub_message, field_desc) <= index)
                                sub_refl->AddMessage(sub_message, field_desc);
                        }

                        sub_message =
                            (field_desc->is_repeated())
                                ? sub_refl->MutableRepeatedMessage(sub_message, field_desc, index)
                                : sub_refl->MutableMessage(sub_message, field_desc);
                        sub_refl = sub_message->GetReflection();
                    }

                    parse(extract, sub_message, "%" + subfields[subfields.size() - 1] + "%",
                          repeated_delimiter, algorithms, use_short_enum);
                }
                else
                {
                    try
                    {
                        std::vector<std::string> field_and_index;
                        boost::split(field_and_index, specifier, boost::is_any_of("."));

                        int field_index = boost::lexical_cast<int>(field_and_index[0]);
                        bool is_indexed_repeated_field = field_and_index.size() == 2;

                        int value_index = 0;
                        if (is_indexed_repeated_field)
                            value_index = boost::lexical_cast<int>(field_and_index[1]);

                        const google::protobuf::FieldDescriptor* field_desc =
                            desc->FindFieldByNumber(field_index);

                        if (!field_desc)
                            throw(std::runtime_error("Bad field: " + specifier +
                                                     " not in message " + desc->full_name()));

                        // run algorithms
                        typedef google::protobuf::RepeatedPtrField<
                            protobuf::TranslatorEntry::CreateParser::Algorithm>::const_iterator
                            const_iterator;

                        for (const_iterator it = algorithms.begin(), n = algorithms.end(); it != n;
                             ++it)
                        {
                            goby::moos::transitional::DCCLMessageVal extract_val(extract);

                            if (it->primary_field() == field_index)
                                moos::transitional::DCCLAlgorithmPerformer::getInstance()
                                    ->run_algorithm(
                                        it->name(), extract_val,
                                        std::vector<goby::moos::transitional::DCCLMessageVal>());

                            extract = std::string(extract_val);
                        }

                        std::vector<std::string> parts;
                        if (is_indexed_repeated_field || !field_desc->is_repeated())
                            parts.push_back(extract);
                        else
                            boost::split(parts, extract, boost::is_any_of(repeated_delimiter));

                        for (int j = 0, m = parts.size(); j < m; ++j)
                        {
                            switch (field_desc->cpp_type())
                            {
                                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddMessage(out, field_desc);
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->MutableRepeatedMessage(out, field_desc,
                                                                              value_index)
                                                     ->ParseFromString(
                                                         goby::util::hex_decode(parts[j]))
                                               : refl->AddMessage(out, field_desc)
                                                     ->ParseFromString(
                                                         goby::util::hex_decode(parts[j])))
                                        : refl->MutableMessage(out, field_desc)
                                              ->ParseFromString(goby::util::hex_decode(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddInt32(out, field_desc,
                                                           field_desc->default_value_int32());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedInt32(
                                                     out, field_desc, value_index,
                                                     goby::util::as<google::protobuf::int32>(
                                                         parts[j]))
                                               : refl->AddInt32(
                                                     out, field_desc,
                                                     goby::util::as<google::protobuf::int32>(
                                                         parts[j])))
                                        : refl->SetInt32(
                                              out, field_desc,
                                              goby::util::as<google::protobuf::int32>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddInt64(out, field_desc,
                                                           field_desc->default_value_int64());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedInt64(
                                                     out, field_desc, value_index,
                                                     goby::util::as<google::protobuf::int64>(
                                                         parts[j]))
                                               : refl->AddInt64(
                                                     out, field_desc,
                                                     goby::util::as<google::protobuf::int64>(
                                                         parts[j])))
                                        : refl->SetInt64(
                                              out, field_desc,
                                              goby::util::as<google::protobuf::int64>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddUInt32(out, field_desc,
                                                            field_desc->default_value_uint32());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedUInt32(
                                                     out, field_desc, value_index,
                                                     goby::util::as<google::protobuf::uint32>(
                                                         parts[j]))
                                               : refl->AddUInt32(
                                                     out, field_desc,
                                                     goby::util::as<google::protobuf::uint32>(
                                                         parts[j])))
                                        : refl->SetUInt32(
                                              out, field_desc,
                                              goby::util::as<google::protobuf::uint32>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddUInt64(out, field_desc,
                                                            field_desc->default_value_uint64());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedUInt64(
                                                     out, field_desc, value_index,
                                                     goby::util::as<google::protobuf::uint64>(
                                                         parts[j]))
                                               : refl->AddUInt64(
                                                     out, field_desc,
                                                     goby::util::as<google::protobuf::uint64>(
                                                         parts[j])))
                                        : refl->SetUInt64(
                                              out, field_desc,
                                              goby::util::as<google::protobuf::uint64>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddBool(out, field_desc,
                                                          field_desc->default_value_bool());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedBool(
                                                     out, field_desc, value_index,
                                                     goby::util::as<bool>(parts[j]))
                                               : refl->AddBool(out, field_desc,
                                                               goby::util::as<bool>(parts[j])))
                                        : refl->SetBool(out, field_desc,
                                                        goby::util::as<bool>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddString(out, field_desc,
                                                            field_desc->default_value_string());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedString(out, field_desc,
                                                                         value_index, parts[j])
                                               : refl->AddString(out, field_desc, parts[j]))
                                        : refl->SetString(out, field_desc, parts[j]);
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddFloat(out, field_desc,
                                                           field_desc->default_value_float());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedFloat(
                                                     out, field_desc, value_index,
                                                     goby::util::as<float>(parts[j]))
                                               : refl->AddFloat(out, field_desc,
                                                                goby::util::as<float>(parts[j])))
                                        : refl->SetFloat(out, field_desc,
                                                         goby::util::as<float>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddDouble(out, field_desc,
                                                            field_desc->default_value_double());
                                    }
                                    field_desc->is_repeated()
                                        ? (is_indexed_repeated_field
                                               ? refl->SetRepeatedDouble(
                                                     out, field_desc, value_index,
                                                     goby::util::as<double>(parts[j]))
                                               : refl->AddDouble(out, field_desc,
                                                                 goby::util::as<double>(parts[j])))
                                        : refl->SetDouble(out, field_desc,
                                                          goby::util::as<double>(parts[j]));
                                    break;

                                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                                {
                                    if (is_indexed_repeated_field)
                                    {
                                        while (refl->FieldSize(*out, field_desc) <= value_index)
                                            refl->AddEnum(out, field_desc,
                                                          field_desc->default_value_enum());
                                    }
                                    std::string enum_value =
                                        ((use_short_enum)
                                             ? add_name_to_enum(parts[j], field_desc->name())
                                             : parts[j]);

                                    const google::protobuf::EnumValueDescriptor* enum_desc =
                                        refl->GetEnum(*out, field_desc)
                                            ->type()
                                            ->FindValueByName(enum_value);

                                    // try upper case
                                    if (!enum_desc)
                                        enum_desc =
                                            refl->GetEnum(*out, field_desc)
                                                ->type()
                                                ->FindValueByName(boost::to_upper_copy(enum_value));
                                    // try lower case
                                    if (!enum_desc)
                                        enum_desc =
                                            refl->GetEnum(*out, field_desc)
                                                ->type()
                                                ->FindValueByName(boost::to_lower_copy(enum_value));
                                    if (enum_desc)
                                    {
                                        field_desc->is_repeated()
                                            ? (is_indexed_repeated_field
                                                   ? refl->SetRepeatedEnum(out, field_desc,
                                                                           value_index, enum_desc)
                                                   : refl->AddEnum(out, field_desc, enum_desc))
                                            : refl->SetEnum(out, field_desc, enum_desc);
                                    }
                                }
                                break;
                            }
                        }
                    }
                    catch (boost::bad_lexical_cast&)
                    {
                        throw(std::runtime_error(
                            "Bad specifier: " + specifier +
                            ", must be an integer. For message: " + desc->full_name()));
                    }
                }

                // goby::glog.is(DEBUG1) && goby::glog << "field: [" << field_index << "], parts[j]: [" << parts[j] << "]" << std::endl;
            }
            else
            {
                // if it's not a %, eat!
                std::string::size_type pos_to_remove = lower_str.find(*i) + 1;
                lower_str.erase(0, pos_to_remove);
                str.erase(0, pos_to_remove);
                ++i;
            }
        }
    }
};
} // namespace moos
} // namespace goby

/// \file moos_protobuf_helpers.h Helpers for MOOS applications for serializing and parsed Google Protocol buffers messages

/// \brief Converts the Google Protocol Buffers message `msg` into a suitable (human readable) string `out` for sending via MOOS
///
/// \param out pointer to std::string to store serialized result
/// \param msg Google Protocol buffers message to serialize
namespace goby
{
namespace moos
{
extern goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique moos_technique;

inline void set_moos_technique(const goby::moos::protobuf::GobyMOOSAppConfig& cfg)
{
    if (cfg.has_moos_parser_technique())
        goby::moos::moos_technique = cfg.moos_parser_technique();
    else if (cfg.has_use_binary_protobuf())
        goby::moos::moos_technique =
            cfg.use_binary_protobuf()
                ? goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED
                : goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT;
}

} // namespace moos
} // namespace goby

inline bool serialize_for_moos(std::string* out, const google::protobuf::Message& msg)
{
    switch (goby::moos::moos_technique)
    {
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::serialize(out, msg);
            return true;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PROTOBUF_NATIVE_HEX>::serialize(out, msg);
            return false;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PROTOBUF_TEXT_FORMAT>::serialize(out, msg);
            return false;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED:
            goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>::
                serialize(out, msg);
            return true;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>::serialize(out,
                                                                                               msg);
            return false;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT:
            goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>::
                serialize(out, msg);
            return false;
        default:
            goby::glog.is(goby::util::logger::DIE) &&
                goby::glog
                    << "Non-PROTOBUF techniques are not supported for 'moos_parser_technique': "
                    << goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                           goby::moos::moos_technique)
                    << std::endl;
            return false;
    }
}

/// \brief Parses the string `in` to Google Protocol Buffers message `msg`. All errors are written to the goby::util::glogger().
///
/// \param in std::string to parse
/// \param msg Google Protocol buffers message to store result
inline void parse_for_moos(const std::string& in, google::protobuf::Message* msg)
{
    switch (goby::moos::moos_technique)
    {
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::parse(in, msg);
            break;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX:
            goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX>::parse(in,
                                                                                             msg);
            break;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT:
            goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT>::parse(in,
                                                                                              msg);
            break;

        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>::parse(in,
                                                                                               msg);
            break;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>::parse(in, msg);
            break;
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT:
            goby::moos::MOOSTranslation<goby::moos::protobuf::TranslatorEntry::
                                            TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>::parse(in,
                                                                                            msg);
            break;
        default:
            goby::glog.is(goby::util::logger::DIE) &&
                goby::glog
                    << "Non-PROTOBUF techniques are not supported for 'moos_parser_technique': "
                    << goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                           goby::moos::moos_technique)
                    << std::endl;
            break;
    }
}

inline std::shared_ptr<google::protobuf::Message> dynamic_parse_for_moos(const std::string& in)
{
    switch (goby::moos::moos_technique)
    {
        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED:
            return goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>::
                dynamic_parse(in);

        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX:
            return goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>::
                dynamic_parse(in);

        case goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT:
            return goby::moos::MOOSTranslation<
                goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>::
                dynamic_parse(in);

        default:
            goby::glog.is(goby::util::logger::DIE) &&
                goby::glog << "Non-PREFIX techniques are not supported when using "
                              "dynamic_parse_for_moos for 'moos_parser_technique': "
                           << goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                                  goby::moos::moos_technique)
                           << std::endl;
            return std::shared_ptr<google::protobuf::Message>();
    }
}

#endif
