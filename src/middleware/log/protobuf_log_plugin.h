// Copyright 2019-2023:
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

#ifndef GOBY_MIDDLEWARE_LOG_PROTOBUF_LOG_PLUGIN_H
#define GOBY_MIDDLEWARE_LOG_PROTOBUF_LOG_PLUGIN_H

#include <google/protobuf/util/json_util.h>

#include "goby/middleware/log.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/protobuf/log_tool_config.pb.h"
#include "goby/time/convert.h"
#include "goby/util/dccl_compat.h"
#include "log_plugin.h"

#if GOOGLE_PROTOBUF_VERSION < 3001000
#define ByteSizeLong ByteSize
#endif

namespace goby
{
namespace middleware
{
namespace log
{
constexpr goby::middleware::Group file_desc_group{"goby::log::ProtobufFileDescriptor"};

/// Implements hooks for Protobuf metadata
template <int scheme> class ProtobufPluginBase : public LogPlugin
{
    static_assert(scheme == goby::middleware::MarshallingScheme::PROTOBUF ||
                      scheme == goby::middleware::MarshallingScheme::DCCL,
                  "Scheme must be PROTOBUF or DCCL");

  public:
    ProtobufPluginBase(bool user_pool_first) : user_pool_first_(user_pool_first) {}

    std::string debug_text_message(LogEntry& log_entry) override
    {
        auto msgs = parse_message(log_entry);

        std::stringstream ss;
        for (typename decltype(msgs)::size_type i = 0, n = msgs.size(); i < n; ++i)
        {
            if (n > 1)
                ss << "[" << i << "]";
            ss << msgs[i]->ShortDebugString();
        }
        return ss.str();
    }

    std::vector<goby::middleware::HDF5ProtobufEntry> hdf5_entry(LogEntry& log_entry) override
    {
        std::vector<goby::middleware::HDF5ProtobufEntry> hdf5_entries;
        auto msgs = parse_message(log_entry);

        for (auto msg : msgs)
        {
            hdf5_entries.emplace_back();
            goby::middleware::HDF5ProtobufEntry& hdf5_entry = hdf5_entries.back();
            hdf5_entry.channel = log_entry.group();
            hdf5_entry.time = goby::time::convert<decltype(hdf5_entry.time)>(log_entry.timestamp());
            hdf5_entry.scheme = scheme;
            hdf5_entry.msg = msg;
        }
        return hdf5_entries;
    }

    std::shared_ptr<nlohmann::json> json_message(LogEntry& log_entry) override
    {
        auto j = std::make_shared<nlohmann::json>();
        auto msgs = parse_message(log_entry);

        for (typename decltype(msgs)::size_type i = 0, n = msgs.size(); i < n; ++i)
        {
            std::string jstr;
            google::protobuf::util::MessageToJsonString(*msgs[i], &jstr);

            if (n > 1)
                (*j)[n] = nlohmann::json::parse(jstr);
            else
                (*j) = nlohmann::json::parse(jstr);
        }
        return j;
    }

    void register_read_hooks(const std::ifstream& in_log_file) override
    {
        LogEntry::filter_hook[{static_cast<int>(scheme), static_cast<std::string>(file_desc_group),
                               google::protobuf::FileDescriptorProto::descriptor()->full_name()}] =
            [&](const std::vector<unsigned char>& data)
        {
            google::protobuf::FileDescriptorProto file_desc_proto;
            file_desc_proto.ParseFromArray(&data[0], data.size());

            if (!read_file_desc_names_.count(file_desc_proto.name()))
            {
                goby::glog.is_debug1() && goby::glog << "Adding: " << file_desc_proto.name()
                                                     << std::endl;

                dccl::DynamicProtobufManager::add_protobuf_file(file_desc_proto);
                read_file_desc_names_.insert(file_desc_proto.name());
            }
        };
    }

    void register_write_hooks(std::ofstream& out_log_file) override
    {
        LogEntry::new_type_hook[scheme] = [&](const std::string& type)
        { add_new_protobuf_type(type, out_log_file); };
    }

    std::vector<std::shared_ptr<google::protobuf::Message>> parse_message(LogEntry& log_entry)
    {
        std::vector<std::shared_ptr<google::protobuf::Message>> msgs;

        const auto& data = log_entry.data();
        auto bytes_begin = data.begin(), bytes_end = data.end(), actual_end = data.begin();

        while (actual_end != bytes_end)
        {
            try
            {
                auto msg = SerializerParserHelper<google::protobuf::Message, scheme>::parse(
                    bytes_begin, bytes_end, actual_end, log_entry.type(), user_pool_first_);

                find_unknown_fields(*msg);
                msgs.push_back(msg);
            }
            catch (std::exception& e)
            {
                throw(log::LogException("Failed to create Protobuf message of type: " +
                                        log_entry.type() + ", reason: " + e.what()));
            }
            bytes_begin = actual_end;
        }

        return msgs;
    }

  private:
    void insert_protobuf_file_desc(const google::protobuf::FileDescriptor* file_desc,
                                   std::ofstream& out_log_file)
    {
        if (written_file_desc_.count(file_desc) == 0)
        {
            for (int i = 0, n = file_desc->dependency_count(); i < n; ++i)
                insert_protobuf_file_desc(file_desc->dependency(i), out_log_file);

            goby::glog.is_debug1() &&
                goby::glog << "Inserting file descriptor proto for: " << file_desc->name() << " : "
                           << file_desc << std::endl;

            written_file_desc_.insert(file_desc);

            google::protobuf::FileDescriptorProto file_desc_proto;
            file_desc->CopyTo(&file_desc_proto);
            std::vector<unsigned char> data(file_desc_proto.ByteSizeLong());
            file_desc_proto.SerializeToArray(&data[0], data.size());
            LogEntry entry(data, goby::middleware::MarshallingScheme::PROTOBUF,
                           google::protobuf::FileDescriptorProto::descriptor()->full_name(),
                           file_desc_group);
            entry.serialize(&out_log_file);
        }
        else
        {
            goby::glog.is_debug2() && goby::glog
                                          << "Skipping already written file descriptor proto for: "
                                          << file_desc->name() << std::endl;
        }
    }

    void add_new_protobuf_type(const std::string& protobuf_type, std::ofstream& out_log_file)
    {
        const google::protobuf::Descriptor* desc =
            dccl::DynamicProtobufManager::find_descriptor(protobuf_type);
        if (!desc)
        {
            goby::glog.is_warn() && goby::glog << "Unknown protobuf type: " << protobuf_type
                                               << std::endl;
        }
        else
        {
            add_new_protobuf_type(desc, out_log_file);
        }
    }

    void add_new_protobuf_type(const google::protobuf::Descriptor* desc,
                               std::ofstream& out_log_file)
    {
        if (written_desc_.count(desc) == 0)
        {
            goby::glog.is_debug1() && goby::glog << "Add new protobuf type: " << desc->full_name()
                                                 << std::endl;

            insert_protobuf_file_desc(desc->file(), out_log_file);

            auto insert_extensions =
                [this, &out_log_file](
                    const std::vector<const google::protobuf::FieldDescriptor*> extensions)
            {
                for (const auto* field_desc : extensions)
                {
                    insert_protobuf_file_desc(field_desc->file(), out_log_file);
                }
            };

            std::vector<const google::protobuf::FieldDescriptor*> generated_extensions;
            google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(
                desc, &generated_extensions);

            for (const auto* desc : generated_extensions)
                goby::glog.is_debug1() && goby::glog << "Found generated extension ["
                                                     << desc->number() << "]: " << desc->full_name()
                                                     << " in file: " << desc->file()->name()
                                                     << std::endl;

            insert_extensions(generated_extensions);

            std::vector<const google::protobuf::FieldDescriptor*> user_extensions;

#ifdef DCCL_VERSION_4_1_OR_NEWER
            dccl::DynamicProtobufManager::user_descriptor_pool_call(
                &google::protobuf::DescriptorPool::FindAllExtensions, desc, &user_extensions);
#else
            dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(
                desc, &user_extensions);
#endif

            for (const auto* desc : user_extensions)
                goby::glog.is_debug1() && goby::glog << "Found user extension [" << desc->number()
                                                     << "]: " << desc->full_name()
                                                     << " in file: " << desc->file()->name()
                                                     << std::endl;
            insert_extensions(user_extensions);

            written_desc_.insert(desc);

            // recursively add children
            for (auto i = 0, n = desc->field_count(); i < n; ++i)
            {
                const auto* field_desc = desc->field(i);
                if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
                    add_new_protobuf_type(field_desc->message_type(), out_log_file);
            }
        }
    }

    void find_unknown_fields(const google::protobuf::Message& msg)
    {
        const auto* refl = msg.GetReflection();
        const auto* desc = msg.GetDescriptor();

        const google::protobuf::UnknownFieldSet& unknown_fields = refl->GetUnknownFields(msg);
        if (!unknown_fields.empty())
        {
            std::string unknown_field_numbers;
            for (int i = 0, n = unknown_fields.field_count(); i < n; ++i)
            {
                const auto& unknown_field = unknown_fields.field(i);
                unknown_field_numbers += std::to_string(unknown_field.number()) + " ";
            }

            goby::glog.is_warn() &&
                goby::glog << "Unknown fields found in " << desc->full_name() << ": "
                           << unknown_field_numbers
                           << ", ensure all extensions are loaded using load_shared_library"
                           << std::endl;
        }
        for (auto i = 0, n = desc->field_count(); i < n; ++i)
        {
            const auto* field_desc = desc->field(i);

            if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
            {
                if (field_desc->is_repeated())
                {
                    for (int i = 0, n = refl->FieldSize(msg, field_desc); i < n; ++i)
                    {
                        find_unknown_fields(refl->GetRepeatedMessage(msg, field_desc, i));
                    }
                }
                else
                {
                    find_unknown_fields(refl->GetMessage(msg, field_desc));
                }
            }
        }
    }

  private:
    std::set<const google::protobuf::FileDescriptor*> written_file_desc_;
    std::set<const google::protobuf::Descriptor*> written_desc_;
    std::set<std::string> read_file_desc_names_;
    bool user_pool_first_;
};

class ProtobufPlugin : public ProtobufPluginBase<goby::middleware::MarshallingScheme::PROTOBUF>
{
  public:
    ProtobufPlugin(bool user_pool_first = false)
        : ProtobufPluginBase<goby::middleware::MarshallingScheme::PROTOBUF>(user_pool_first)
    {
    }
};

} // namespace log
} // namespace middleware
} // namespace goby

#endif
