// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef PROTOBUF_LOGGER_20190123_H
#define PROTOBUF_LOGGER_20190123_H

#include "goby/middleware/log.h"
#include "goby/middleware/protobuf/log_tool_config.pb.h"
#include "goby/middleware/serialize_parse.h"
#include "logger_plugin.h"

namespace goby
{
namespace logger
{
constexpr goby::Group file_desc_group{"goby::logger::ProtobufFileDescriptor"};

/// Implements hooks for Protobuf metadata
class ProtobufPluginBase : public LogPlugin
{
  public:
    std::string debug_text_message(goby::LogEntry& log_entry) override
    {
        auto desc = dccl::DynamicProtobufManager::find_descriptor(log_entry.type());

        if (!desc)
            throw(logger::LogException("Failed to find Descriptor for Protobuf message of type: " +
                                       log_entry.type()));

        auto msg = dccl::DynamicProtobufManager::new_protobuf_message<
            std::unique_ptr<google::protobuf::Message> >(desc);

        if (!msg)
            throw(logger::LogException("Failed to create Protobuf message of type: " +
                                       desc->full_name()));

        parse_message(log_entry, msg.get());

        std::stringstream ss;
        ss << msg->ShortDebugString();
        return ss.str();
    }

    void register_read_hooks(const std::ifstream& in_log_file) override
    {
        goby::LogEntry::filter_hook[{
            static_cast<int>(goby::MarshallingScheme::PROTOBUF),
            static_cast<std::string>(file_desc_group),
            google::protobuf::FileDescriptorProto::descriptor()->full_name()}] =
            [](const std::vector<unsigned char>& data) {
                google::protobuf::FileDescriptorProto file_desc_proto;
                file_desc_proto.ParseFromArray(&data[0], data.size());
                goby::glog.is_debug1() && goby::glog << "Adding: " << file_desc_proto.name()
                                                     << std::endl;
                dccl::DynamicProtobufManager::add_protobuf_file(file_desc_proto);
            };
    }

  protected:
    template <int scheme> void register_write_hooks(std::ofstream& out_log_file)
    {
        static_assert(scheme == goby::MarshallingScheme::PROTOBUF ||
                          scheme == goby::MarshallingScheme::DCCL,
                      "Scheme must be PROTOBUF or DCCL");

        LogEntry::new_type_hook[scheme] = [&](const std::string& type) {
            add_new_protobuf_type(scheme, type, out_log_file);
        };
    }

  private:
    virtual void parse_message(goby::LogEntry& log_entry, google::protobuf::Message* msg) = 0;

    void insert_protobuf_file_desc(const google::protobuf::FileDescriptor* file_desc,
                                   std::ofstream& out_log_file);

    void add_new_protobuf_type(int scheme, const std::string& protobuf_type,
                               std::ofstream& out_log_file);

  private:
    std::set<const google::protobuf::FileDescriptor*> written_file_desc_;
};

class ProtobufPlugin : public ProtobufPluginBase
{
  public:
    void register_write_hooks(std::ofstream& out_log_file) override
    {
        ProtobufPluginBase::register_write_hooks<goby::MarshallingScheme::PROTOBUF>(out_log_file);
    }

  private:
    void parse_message(goby::LogEntry& log_entry, google::protobuf::Message* msg) override
    {
        msg->ParseFromArray(&log_entry.data()[0], log_entry.data().size());
    }
};

} // namespace logger
} // namespace goby

#endif
