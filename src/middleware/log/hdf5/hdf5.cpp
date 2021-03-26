// Copyright 2016-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Shawn Dooley <shawn@shawndooley.net>
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

#include <cstddef> // for size_t
#include <cstdint>  // for uint64_t, int...

#include <boost/algorithm/string/classification.hpp>   // for is_any_ofF
#include <boost/algorithm/string/predicate_facade.hpp> // for predicate_facade
#include <boost/algorithm/string/split.hpp>            // for split
#include <boost/algorithm/string/trim.hpp>             // for trim_copy_if
#include <dccl/dynamic_protobuf_manager.h>             // for DynamicProtob...

#include "goby/time/types.h" // for MicroTime

#include "hdf5.h"
#include "hdf5_plugin.h" // for HDF5ProtobufE...

void goby::middleware::hdf5::Channel::add_message(const goby::middleware::HDF5ProtobufEntry& entry)
{
    const std::string& msg_name = entry.msg->GetDescriptor()->full_name();
    typedef std::map<std::string, MessageCollection>::iterator It;
    auto it = entries.find(msg_name);
    if (it == entries.end())
    {
        std::pair<It, bool> itpair =
            entries.insert(std::make_pair(msg_name, MessageCollection(msg_name)));
        it = itpair.first;
    }
    it->second.entries.insert(std::make_pair(time::MicroTime(entry.time).value(), entry.msg));
}

H5::Group& goby::middleware::hdf5::GroupFactory::fetch_group(const std::string& group_path)
{
    std::deque<std::string> nodes;
    std::string clean_path = boost::trim_copy_if(group_path, boost::algorithm::is_space() ||
                                                                 boost::algorithm::is_any_of("/"));
    boost::split(nodes, clean_path, boost::is_any_of("/"));
    return root_group_.fetch_group(nodes);
}

H5::Group&
goby::middleware::hdf5::GroupFactory::GroupWrapper::fetch_group(std::deque<std::string>& nodes)
{
    if (nodes.empty())
    {
        return group_;
    }
    else
    {
        using It = std::map<std::string, GroupWrapper>::iterator;
        auto it = children_.find(nodes[0]);
        if (it == children_.end())
        {
            std::pair<It, bool> itpair =
                children_.insert(std::make_pair(nodes[0], GroupWrapper(nodes[0], group_)));
            it = itpair.first;
        }
        nodes.pop_front();
        return it->second.fetch_group(nodes);
    }
}

goby::middleware::hdf5::Writer::Writer(const std::string& output_file)
    : h5file_(output_file, H5F_ACC_TRUNC), group_factory_(h5file_)
{
}

void goby::middleware::hdf5::Writer::add_entry(goby::middleware::HDF5ProtobufEntry entry)
{
    boost::trim_if(entry.channel, boost::algorithm::is_space() || boost::algorithm::is_any_of("/"));

    using It = std::map<std::string, goby::middleware::hdf5::Channel>::iterator;
    auto it = channels_.find(entry.channel);
    if (it == channels_.end())
    {
        std::pair<It, bool> itpair = channels_.insert(
            std::make_pair(entry.channel, goby::middleware::hdf5::Channel(entry.channel)));
        it = itpair.first;
    }

    it->second.add_message(entry);
}

void goby::middleware::hdf5::Writer::write()
{
    for (const auto& channel : channels_) write_channel("/" + channel.first, channel.second);
}

void goby::middleware::hdf5::Writer::write_channel(const std::string& group,
                                                   const goby::middleware::hdf5::Channel& channel)
{
    for (const auto& entry : channel.entries)
        write_message_collection(group + "/" + entry.first, entry.second);
}

void goby::middleware::hdf5::Writer::write_message_collection(
    const std::string& group, const goby::middleware::hdf5::MessageCollection& message_collection)
{
    write_time(group, message_collection);

    auto write_field = [&, this](const google::protobuf::FieldDescriptor* field_desc) {
        std::vector<const google::protobuf::Message*> messages;
        for (const auto& entry : message_collection.entries)
        { messages.push_back(entry.second.get()); } std::vector<hsize_t> hs;
        hs.push_back(messages.size());
        write_field_selector(group, field_desc, messages, hs);
    };

    const google::protobuf::Descriptor* desc =
        message_collection.entries.begin()->second->GetDescriptor();
    for (int i = 0, n = desc->field_count(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
        write_field(field_desc);
    }

    std::vector<const google::protobuf::FieldDescriptor*> extensions;
    google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(desc, &extensions);
    dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(desc, &extensions);

    for (auto field_desc : extensions) { write_field(field_desc); }
}

void goby::middleware::hdf5::Writer::write_embedded_message(
    const std::string& group, const google::protobuf::FieldDescriptor* field_desc,
    const std::vector<const google::protobuf::Message*> messages, std::vector<hsize_t>& hs)
{
    const google::protobuf::Descriptor* sub_desc = field_desc->message_type();
    if (field_desc->is_repeated())
    {
        int max_field_size = 0;
        for (auto message : messages)
        {
            if (message)
            {
                const google::protobuf::Reflection* refl = message->GetReflection();
                int field_size = refl->FieldSize(*message, field_desc);
                if (field_size > max_field_size)
                    max_field_size = field_size;
            }
        }

        hs.push_back(max_field_size);

        auto write_field = [&, this](const google::protobuf::FieldDescriptor* sub_field_desc) {
            std::vector<const google::protobuf::Message*> sub_messages(
                messages.size() * max_field_size, (const google::protobuf::Message*)nullptr);

            bool has_submessages = false;
            for (unsigned i = 0, n = messages.size(); i < n; ++i)
            {
                if (messages[i])
                {
                    const google::protobuf::Reflection* refl = messages[i]->GetReflection();
                    int field_size = refl->FieldSize(*messages[i], field_desc);

                    for (int j = 0; j < field_size; ++j)
                    {
                        const google::protobuf::Message& sub_msg =
                            refl->GetRepeatedMessage(*messages[i], field_desc, j);
                        sub_messages[i * max_field_size + j] = &sub_msg;
                    }
                    has_submessages = true;
                }
            }

            if (has_submessages) // don't recurse unless one or more message requires it
                write_field_selector(
                    group + "/" +
                        (field_desc->is_extension() ? field_desc->full_name() : field_desc->name()),
                    sub_field_desc, sub_messages, hs);
        };

        for (int i = 0, n = sub_desc->field_count(); i < n; ++i)
        {
            const google::protobuf::FieldDescriptor* sub_field_desc = sub_desc->field(i);
            write_field(sub_field_desc);
        }

        std::vector<const google::protobuf::FieldDescriptor*> extensions;
        google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(sub_desc,
                                                                              &extensions);
        dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(sub_desc,
                                                                               &extensions);
        for (auto sub_field_desc : extensions) { write_field(sub_field_desc); }

        hs.pop_back();
    }
    else
    {
        auto write_field = [&, this](const google::protobuf::FieldDescriptor* sub_field_desc) {
            std::vector<const google::protobuf::Message*> sub_messages;

            bool has_submessages = false;

            for (auto message : messages)
            {
                if (message)
                {
                    const google::protobuf::Reflection* refl = message->GetReflection();
                    const google::protobuf::Message& sub_msg =
                        refl->GetMessage(*message, field_desc);
                    sub_messages.push_back(&sub_msg);
                    has_submessages = true;
                }
                else
                {
                    sub_messages.push_back(nullptr);
                }
            }

            if (has_submessages)
                write_field_selector(
                    group + "/" +
                        (field_desc->is_extension() ? field_desc->full_name() : field_desc->name()),
                    sub_field_desc, sub_messages, hs);
        };

        for (int i = 0, n = sub_desc->field_count(); i < n; ++i)
        {
            const google::protobuf::FieldDescriptor* sub_field_desc = sub_desc->field(i);
            write_field(sub_field_desc);
        }

        std::vector<const google::protobuf::FieldDescriptor*> extensions;
        google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(sub_desc,
                                                                              &extensions);
        dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(sub_desc,
                                                                               &extensions);
        for (auto sub_field_desc : extensions) { write_field(sub_field_desc); }
    }
}

void goby::middleware::hdf5::Writer::write_field_selector(
    const std::string& group, const google::protobuf::FieldDescriptor* field_desc,
    const std::vector<const google::protobuf::Message*>& messages, std::vector<hsize_t>& hs)
{
    switch (field_desc->cpp_type())
    {
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            write_embedded_message(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
        {
            // google uses int for the enum value type, we'll assume that's an int32 here
            write_field<std::int32_t>(group, field_desc, messages, hs);
            write_enum_attributes(group, field_desc);
            break;
        }

        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            write_field<std::int32_t>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            write_field<std::int64_t>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            write_field<std::uint32_t>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            write_field<std::uint64_t>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
            write_field<unsigned char>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
            write_field<std::string>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            write_field<float>(group, field_desc, messages, hs);
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
            write_field<double>(group, field_desc, messages, hs);
            break;
    }
}

void goby::middleware::hdf5::Writer::write_enum_attributes(
    const std::string& group, const google::protobuf::FieldDescriptor* field_desc)
{
    // write enum names and values to attributes
    H5::Group& grp = group_factory_.fetch_group(group);
    H5::DataSet ds = grp.openDataSet(field_desc->name());

    const google::protobuf::EnumDescriptor* enum_desc = field_desc->enum_type();

    std::vector<const char*> names(enum_desc->value_count(), (const char*)nullptr);
    std::vector<std::int32_t> values(enum_desc->value_count(), 0);

    for (int i = 0, n = enum_desc->value_count(); i < n; ++i)
    {
        names[i] = enum_desc->value(i)->name().c_str();
        values[i] = enum_desc->value(i)->number();
    }

    {
        const int rank = 1;
        hsize_t hs[] = {names.size()};
        H5::DataSpace att_space(rank, hs, hs);
        H5::StrType att_type(H5::PredType::C_S1, H5T_VARIABLE);
        H5::Attribute att = ds.createAttribute("enum_names", att_type, att_space);
        att.write(att_type, &names[0]);
    }
    {
        const int rank = 1;
        hsize_t hs[] = {values.size()};
        H5::DataSpace att_space(rank, hs, hs);
        H5::IntType att_type(predicate<std::int32_t>());
        H5::Attribute att = ds.createAttribute("enum_values", att_type, att_space);
        att.write(att_type, &values[0]);
    }
}

void goby::middleware::hdf5::Writer::write_time(
    const std::string& group, const goby::middleware::hdf5::MessageCollection& message_collection)
{
    std::vector<std::uint64_t> utime(message_collection.entries.size(), 0);
    std::vector<double> datenum(message_collection.entries.size(), 0);
    int i = 0;
    for (const auto& entry : message_collection.entries)
    {
        utime[i] = entry.first;
        // datenum(1970, 1, 1, 0, 0, 0)
        const double datenum_unix_epoch = 719529;
        const double seconds_in_day = 86400;
        std::uint64_t utime_sec = utime[i] / 1000000;
        std::uint64_t utime_frac = utime[i] - utime_sec * 1000000;
        datenum[i] = datenum_unix_epoch + static_cast<double>(utime_sec) / seconds_in_day +
                     static_cast<double>(utime_frac) / 1000000 / seconds_in_day;
        ++i;
    }

    std::vector<hsize_t> hs;
    hs.push_back(message_collection.entries.size());
    write_vector(group, "_utime_", utime, hs, (std::uint64_t)0);
    write_vector(group, "_datenum_", datenum, hs, (double)0);
}

void goby::middleware::hdf5::Writer::write_vector(const std::string& group,
                                                  const std::string& dataset_name,
                                                  const std::vector<std::string>& data,
                                                  const std::vector<hsize_t>& hs_outer,
                                                  const std::string& default_value)
{
    std::vector<char> data_char;
    std::vector<hsize_t> hs = hs_outer;

    size_t max_size = 0;
    for (const auto& i : data)
    {
        if (i.size() > max_size)
            max_size = i.size();
    }

    for (auto d : data)
    {
        d.resize(max_size, ' ');
        for (char c : d) data_char.push_back(c);
    }
    hs.push_back(max_size);

    H5::DataSpace dataspace(hs.size(), hs.data(), hs.data());
    H5::Group& grp = group_factory_.fetch_group(group);
    H5::DataSet dataset = grp.createDataSet(dataset_name, H5::PredType::NATIVE_CHAR, dataspace);

    if (data_char.size())
        dataset.write(&data_char[0], H5::PredType::NATIVE_CHAR);

    const int rank = 1;
    hsize_t att_hs[] = {1};
    H5::DataSpace att_space(rank, att_hs, att_hs);
    H5::StrType att_datatype(H5::PredType::C_S1, default_value.size() + 1);
    H5::Attribute att = dataset.createAttribute("default_value", att_datatype, att_space);
    const H5std_string& strbuf(default_value);
    att.write(att_datatype, strbuf);
}
