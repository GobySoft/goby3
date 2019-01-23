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

#include "protobuf_logger_plugin.h"

void goby::logger::ProtobufPluginBase::insert_protobuf_file_desc(
    const google::protobuf::FileDescriptor* file_desc, std::ofstream& out_log_file)
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
        goby::LogEntry entry(data, goby::MarshallingScheme::PROTOBUF,
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

void goby::logger::ProtobufPluginBase::add_new_protobuf_type(int scheme,
                                                             const std::string& protobuf_type,
                                                             std::ofstream& out_log_file)
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
