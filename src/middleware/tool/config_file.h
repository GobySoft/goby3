// Copyright 2026:
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

#ifndef GOBY_MIDDLEWARE_TOOL_CONFIG_FILE_H
#define GOBY_MIDDLEWARE_TOOL_CONFIG_FILE_H

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#ifdef HAS_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

#include "goby/exception.h"

namespace goby
{
namespace middleware
{
namespace tool
{
#ifdef HAS_YAML_CPP
namespace detail
{
inline std::string yaml_path(const std::string& parent, const std::string& key)
{
    return parent.empty() ? key : parent + "." + key;
}

inline void yaml_to_protobuf(const YAML::Node& node, google::protobuf::Message& msg,
                             const std::string& path);

/// a map, or a repeated message with exactly 'key' and 'value' fields, can be written as a YAML map
inline bool is_key_value_list(const google::protobuf::FieldDescriptor* field)
{
    if (field->is_map())
        return true;
    if (!field->is_repeated() ||
        field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        return false;
    const auto* type = field->message_type();
    return type->field_count() == 2 && type->FindFieldByName("key") &&
           type->FindFieldByName("value");
}

inline void yaml_scalar_to_field(const YAML::Node& node, google::protobuf::Message& msg,
                                 const google::protobuf::FieldDescriptor* field,
                                 const std::string& path)
{
    using google::protobuf::FieldDescriptor;
    const auto* refl = msg.GetReflection();
    const bool repeated = field->is_repeated();
    const std::string text = node.as<std::string>();

    auto fail = [&](const std::string& what)
    { throw goby::Exception(path + ": " + what + " (got '" + text + "')"); };

    try
    {
        switch (field->cpp_type())
        {
            case FieldDescriptor::CPPTYPE_INT32:
                repeated ? refl->AddInt32(&msg, field, node.as<std::int32_t>())
                         : refl->SetInt32(&msg, field, node.as<std::int32_t>());
                break;
            case FieldDescriptor::CPPTYPE_INT64:
                repeated ? refl->AddInt64(&msg, field, node.as<std::int64_t>())
                         : refl->SetInt64(&msg, field, node.as<std::int64_t>());
                break;
            case FieldDescriptor::CPPTYPE_UINT32:
                repeated ? refl->AddUInt32(&msg, field, node.as<std::uint32_t>())
                         : refl->SetUInt32(&msg, field, node.as<std::uint32_t>());
                break;
            case FieldDescriptor::CPPTYPE_UINT64:
                repeated ? refl->AddUInt64(&msg, field, node.as<std::uint64_t>())
                         : refl->SetUInt64(&msg, field, node.as<std::uint64_t>());
                break;
            case FieldDescriptor::CPPTYPE_DOUBLE:
                repeated ? refl->AddDouble(&msg, field, node.as<double>())
                         : refl->SetDouble(&msg, field, node.as<double>());
                break;
            case FieldDescriptor::CPPTYPE_FLOAT:
                repeated ? refl->AddFloat(&msg, field, node.as<float>())
                         : refl->SetFloat(&msg, field, node.as<float>());
                break;
            case FieldDescriptor::CPPTYPE_BOOL:
                repeated ? refl->AddBool(&msg, field, node.as<bool>())
                         : refl->SetBool(&msg, field, node.as<bool>());
                break;
            case FieldDescriptor::CPPTYPE_STRING:
                repeated ? refl->AddString(&msg, field, text) : refl->SetString(&msg, field, text);
                break;
            case FieldDescriptor::CPPTYPE_ENUM:
            {
                const auto* value = field->enum_type()->FindValueByName(text);
                if (!value && std::regex_match(text, std::regex("-?[0-9]+")))
                    value = field->enum_type()->FindValueByNumber(std::stoi(text));
                if (!value)
                    fail("not a value of enum " + field->enum_type()->full_name());
                repeated ? refl->AddEnum(&msg, field, value) : refl->SetEnum(&msg, field, value);
                break;
            }
            case FieldDescriptor::CPPTYPE_MESSAGE:
                fail("expected a map for message field " + field->message_type()->full_name());
        }
    }
    catch (const YAML::Exception&)
    {
        fail("not a valid " + std::string(field->cpp_type_name()));
    }
}

inline void yaml_to_field(const YAML::Node& node, google::protobuf::Message& msg,
                          const google::protobuf::FieldDescriptor* field, const std::string& path)
{
    using google::protobuf::FieldDescriptor;
    const auto* refl = msg.GetReflection();

    if (node.IsNull())
        return;

    if (detail::is_key_value_list(field) && node.IsMap())
    {
        if (!node.IsMap())
            throw goby::Exception(path + ": expected a map");
        const auto* entry_desc = field->message_type();
        for (const auto& kv : node)
        {
            const std::string key = kv.first.as<std::string>();
            auto& entry = *refl->AddMessage(&msg, field);
            yaml_scalar_to_field(kv.first, entry, entry_desc->FindFieldByName("key"),
                                 path + "." + key);
            yaml_to_field(kv.second, entry, entry_desc->FindFieldByName("value"), path + "." + key);
        }
        return;
    }

    if (field->is_repeated())
    {
        // a scalar or map where a list is expected is treated as a one element list
        if (!node.IsSequence())
        {
            yaml_to_field(YAML::Node(YAML::NodeType::Sequence), msg, field, path);
            if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
                yaml_to_protobuf(node, *refl->AddMessage(&msg, field), path);
            else
                yaml_scalar_to_field(node, msg, field, path);
            return;
        }
        int i = 0;
        for (const auto& item : node)
        {
            const std::string item_path = path + "[" + std::to_string(i++) + "]";
            if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
                yaml_to_protobuf(item, *refl->AddMessage(&msg, field), item_path);
            else if (item.IsScalar())
                yaml_scalar_to_field(item, msg, field, item_path);
            else
                throw goby::Exception(item_path + ": expected a scalar");
        }
    }
    else if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
    {
        yaml_to_protobuf(node, *refl->MutableMessage(&msg, field), path);
    }
    else if (node.IsScalar())
    {
        yaml_scalar_to_field(node, msg, field, path);
    }
    else
    {
        throw goby::Exception(path + ": expected a scalar");
    }
}

inline void yaml_to_protobuf(const YAML::Node& node, google::protobuf::Message& msg,
                             const std::string& path)
{
    if (node.IsNull())
        return;
    if (!node.IsMap())
        throw goby::Exception((path.empty() ? std::string("top level") : path) +
                              ": expected a map for message " + msg.GetDescriptor()->full_name());

    const auto* desc = msg.GetDescriptor();

    // a message that only wraps a map can be written as the map itself
    if (desc->field_count() == 1 && is_key_value_list(desc->field(0)) &&
        !(node[desc->field(0)->name()] || node[desc->field(0)->camelcase_name()]))
    {
        yaml_to_field(node, msg, desc->field(0), path);
        return;
    }

    for (const auto& kv : node)
    {
        const std::string key = kv.first.as<std::string>();
        const auto* field = desc->FindFieldByName(key);
        if (!field)
            field = desc->FindFieldByCamelcaseName(key);
        if (!field)
            throw goby::Exception(yaml_path(path, key) + " (line " +
                                  std::to_string(kv.first.Mark().line + 1) +
                                  "): no such field in " + desc->full_name());
        yaml_to_field(kv.second, msg, field, yaml_path(path, key));
    }
}
} // namespace detail

/// \brief Merge a YAML document into a Protobuf message using the message descriptor.
///
/// Field names may be given as in the .proto file or in camelCase; enums by name or number.
/// A scalar or map given for a repeated field is treated as a single element list.
inline void yaml_to_protobuf(const std::string& yaml, google::protobuf::Message& msg)
{
    YAML::Node root;
    try
    {
        root = YAML::Load(yaml);
    }
    catch (const YAML::Exception& e)
    {
        throw goby::Exception(std::string("YAML parse error: ") + e.what());
    }
    detail::yaml_to_protobuf(root, msg, "");
}
#endif // HAS_YAML_CPP

/// \brief True if YAML configuration files are supported by this build
inline constexpr bool yaml_supported()
{
#ifdef HAS_YAML_CPP
    return true;
#else
    return false;
#endif
}

namespace detail
{
class CollectingErrorCollector : public google::protobuf::io::ErrorCollector
{
  public:
    void AddError(int line, int column, const std::string& message) override
    {
        errors.push_back("line " + std::to_string(line + 1) + " column " +
                         std::to_string(column + 1) + ": " + message);
    }
    void AddWarning(int, int, const std::string&) override {}
    std::vector<std::string> errors;
};
} // namespace detail

/// \brief Merge configuration text into a Protobuf message.
///
/// \param contents The configuration text
/// \param path Used to choose the format by extension (.yml/.yaml is YAML when built with
///        yaml-cpp, .json is JSON, anything else is Protobuf TextFormat)
/// \throw goby::Exception describing the parse error(s)
inline void parse_config_string(const std::string& contents, const std::string& path,
                                google::protobuf::Message& msg)
{
    auto ends_with = [&](const std::string& suffix)
    {
        return path.size() >= suffix.size() &&
               path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (ends_with(".yml") || ends_with(".yaml"))
    {
#ifdef HAS_YAML_CPP
        yaml_to_protobuf(contents, msg);
#else
        throw goby::Exception("YAML support is not compiled in (install libyaml-cpp-dev and "
                              "build with -Denable_yaml=ON), or use a JSON or TextFormat file");
#endif
    }
    else if (ends_with(".json"))
    {
        google::protobuf::util::JsonParseOptions opts;
        auto status = google::protobuf::util::JsonStringToMessage(contents, &msg, opts);
        if (!status.ok())
            throw goby::Exception("JSON parse error: " + status.ToString());
    }
    else
    {
        google::protobuf::TextFormat::Parser parser;
        detail::CollectingErrorCollector error_collector;
        parser.RecordErrorsTo(&error_collector);
        if (!parser.MergeFromString(contents, &msg) || !error_collector.errors.empty())
        {
            std::string what = "TextFormat parse error";
            for (const auto& e : error_collector.errors) what += "; " + e;
            throw goby::Exception(what);
        }
    }
}

/// \brief Merge a configuration file into a Protobuf message (see parse_config_string).
inline void load_config_file(const std::string& path, google::protobuf::Message& msg)
{
    std::ifstream fin(path);
    if (!fin.is_open())
        throw goby::Exception("could not open '" + path + "' for reading");
    std::stringstream ss;
    ss << fin.rdbuf();
    parse_config_string(ss.str(), path, msg);
}

} // namespace tool
} // namespace middleware
} // namespace goby

#endif
