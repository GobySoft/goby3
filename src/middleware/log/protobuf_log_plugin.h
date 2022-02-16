// Copyright 2019-2022:
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

#include "goby/middleware/log.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/protobuf/log_tool_config.pb.h"
#include "goby/time/convert.h"
#include "log_plugin.h"

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

    void register_read_hooks(const std::ifstream& in_log_file) override
    {
        LogEntry::filter_hook[{static_cast<int>(scheme), static_cast<std::string>(file_desc_group),
                               google::protobuf::FileDescriptorProto::descriptor()->full_name()}] =
            [&](const std::vector<unsigned char>& data) {
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
        LogEntry::new_type_hook[scheme] = [&](const std::string& type) {
            add_new_protobuf_type(type, out_log_file);
        };
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
                    bytes_begin, bytes_end, actual_end, log_entry.type());
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
        for (int i = 0, n = file_desc->dependency_count(); i < n; ++i)
            insert_protobuf_file_desc(file_desc->dependency(i), out_log_file);

        if (written_file_desc_.count(file_desc) == 0)
        {
            goby::glog.is_debug1() &&
                goby::glog << "Inserting file descriptor proto for: " << file_desc->name() << " : "
                           << file_desc << std::endl;

            written_file_desc_.insert(file_desc);

            google::protobuf::FileDescriptorProto file_desc_proto;
            file_desc->CopyTo(&file_desc_proto);
            std::vector<unsigned char> data(file_desc_proto.ByteSize());
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
        auto desc = dccl::DynamicProtobufManager::find_descriptor(protobuf_type);
        if (!desc)
        {
            goby::glog.is_warn() && goby::glog << "Unknown protobuf type: " << protobuf_type
                                               << std::endl;
        }
        else
        {
            insert_protobuf_file_desc(desc->file(), out_log_file);
        }
    }

  private:
    std::set<const google::protobuf::FileDescriptor*> written_file_desc_;
    std::set<std::string> read_file_desc_names_;
};

class ProtobufPlugin : public ProtobufPluginBase<goby::middleware::MarshallingScheme::PROTOBUF>
{
};

} // namespace log
} // namespace middleware
} // namespace goby

#endif
