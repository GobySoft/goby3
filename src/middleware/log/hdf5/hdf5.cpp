// Copyright 2016-2023:
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
#include <cstdint> // for uint64_t, int...

#include <boost/algorithm/string/classification.hpp>   // for is_any_ofF
#include <boost/algorithm/string/predicate_facade.hpp> // for predicate_facade
#include <boost/algorithm/string/split.hpp>            // for split
#include <boost/algorithm/string/trim.hpp>             // for trim_copy_if

#include <dccl/dynamic_protobuf_manager.h> // for DynamicProtob...

#include "goby/time/types.h" // for MicroTime
#include "goby/util/dccl_compat.h"
#include "goby/util/debug_logger.h"

#include "hdf5.h"
#include "hdf5_plugin.h" // for HDF5ProtobufE...

size_t
goby::middleware::hdf5::Channel::add_message(const goby::middleware::HDF5ProtobufEntry& entry)
{
    const std::string& msg_name = entry.msg->GetDescriptor()->full_name();
    typedef std::map<std::string, MessageCollection>::iterator It;
    auto it = entries.find(msg_name);
    if (it == entries.end())
    {
        std::pair<It, bool> itpair =
            entries.insert(std::make_pair(msg_name, MessageCollection(msg_name, this->group)));
        it = itpair.first;
    }
    it->second.entries.insert(std::make_pair(time::MicroTime(entry.time).value(), entry));
    return it->second.entries.size();
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

goby::middleware::hdf5::Writer::Writer(const std::string& output_file, bool write_zero_length_dim,
                                       bool use_chunks, hsize_t chunk_length)
    : h5file_(output_file, H5F_ACC_TRUNC),
      group_factory_(h5file_),
      write_zero_length_dim_(write_zero_length_dim),
      use_chunks_(use_chunks),
      chunk_length_(chunk_length)
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

    auto channel_size = it->second.add_message(entry);
    if (use_chunks_ && channel_size >= chunk_length_)
    {
        write_channel(it->second);
        it->second.entries.clear();
    }
}

void goby::middleware::hdf5::Writer::write()
{
    for (const auto& channel : channels_) write_channel(channel.second);
}

void goby::middleware::hdf5::Writer::write_channel(const goby::middleware::hdf5::Channel& channel)
{
    const auto& group = channel.group;
    glog.is_verbose() && glog << "Writing HDF5 group: " << group << std::endl;

    for (const auto& entry : channel.entries) write_message_collection(entry.second);
}

void goby::middleware::hdf5::Writer::write_message_collection(
    const goby::middleware::hdf5::MessageCollection& message_collection)
{
    const auto& group = message_collection.group;
    glog.is_verbose() && glog << "Writing HDF5 group: " << group << std::endl;
    write_time(group, message_collection);
    write_scheme(group, message_collection);

    auto write_field = [&, this](const google::protobuf::FieldDescriptor* field_desc) {
        std::vector<const google::protobuf::Message*> messages;
        for (const auto& entry : message_collection.entries)
        { messages.push_back(entry.second.msg.get()); }
        std::vector<hsize_t> hs;
        hs.push_back(messages.size());
        write_field_selector(group, field_desc, messages, hs);
    };

    const google::protobuf::Descriptor* desc =
        message_collection.entries.begin()->second.msg->GetDescriptor();
    for (int i = 0, n = desc->field_count(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
        write_field(field_desc);
    }

    std::vector<const google::protobuf::FieldDescriptor*> extensions;
    google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(desc, &extensions);

#ifdef DCCL_VERSION_4_1_OR_NEWER
    dccl::DynamicProtobufManager::user_descriptor_pool_call(
        &google::protobuf::DescriptorPool::FindAllExtensions, desc, &extensions);
#else
    dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(desc, &extensions);
#endif

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
#ifdef DCCL_VERSION_4_1_OR_NEWER
        dccl::DynamicProtobufManager::user_descriptor_pool_call(
            &google::protobuf::DescriptorPool::FindAllExtensions, sub_desc, &extensions);
#else
        dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(sub_desc,
                                                                               &extensions);

#endif
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
#ifdef DCCL_VERSION_4_1_OR_NEWER
        dccl::DynamicProtobufManager::user_descriptor_pool_call(
            &google::protobuf::DescriptorPool::FindAllExtensions, sub_desc, &extensions);
#else
        dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(sub_desc,
                                                                               &extensions);
#endif
        for (auto sub_field_desc : extensions) { write_field(sub_field_desc); }
    }
}

void goby::middleware::hdf5::Writer::write_field_selector(
    const std::string& group, const google::protobuf::FieldDescriptor* field_desc,
    const std::vector<const google::protobuf::Message*>& messages, std::vector<hsize_t>& hs)
{
    glog.is_debug1() && glog << "Writing HDF5 group: " << group << std::endl;
    glog.is_debug1() && glog << "Writing field \"" << field_desc->name()
                             << "\" (size: " << dim_str(hs) << ")" << std::endl;

    switch (field_desc->cpp_type())
    {
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            if (field_desc->message_type()->full_name() == "google.protobuf.FileDescriptorProto")
                glog.is_warn() && glog << "Omitting google.protobuf.FileDescriptorProto"
                                       << std::endl;
            else
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

    const char* enum_names_attr_name = "enum_names";
    const char* enum_values_attr_name = "enum_values";
    if (ds.attrExists(enum_names_attr_name) && ds.attrExists(enum_values_attr_name))
        return;

    const google::protobuf::EnumDescriptor* enum_desc = field_desc->enum_type();

    std::vector<const char*> names(enum_desc->value_count(), (const char*)nullptr);
    std::vector<std::int32_t> values(enum_desc->value_count(), 0);

    for (int i = 0, n = enum_desc->value_count(); i < n; ++i)
    {
        names[i] = enum_desc->value(i)->name().c_str();
        values[i] = enum_desc->value(i)->number();
    }

    if (!ds.attrExists(enum_names_attr_name))
    {
        const int rank = 1;
        hsize_t hs[] = {names.size()};
        H5::DataSpace att_space(rank, hs, hs);
        H5::StrType att_type(H5::PredType::C_S1, H5T_VARIABLE);
        H5::Attribute att = ds.createAttribute("enum_names", att_type, att_space);
        att.write(att_type, &names[0]);
    }

    if (!ds.attrExists(enum_values_attr_name))
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
    glog.is_debug1() && glog << "Writing time (size: " << message_collection.entries.size() << ")"
                             << std::endl;

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

void goby::middleware::hdf5::Writer::write_scheme(
    const std::string& group, const goby::middleware::hdf5::MessageCollection& message_collection)
{
    glog.is_debug1() && glog << "Writing scheme (size: " << message_collection.entries.size() << ")"
                             << std::endl;

    std::vector<int> scheme(message_collection.entries.size(), 0);
    int i = 0;
    for (const auto& entry : message_collection.entries)
    {
        scheme[i] = entry.second.scheme;
        ++i;
    }

    std::vector<hsize_t> hs;
    hs.push_back(message_collection.entries.size());
    write_vector(group, "_scheme_", scheme, hs, (int)0);
}

void goby::middleware::hdf5::Writer::write_vector(const std::string& group,
                                                  const std::string& dataset_name,
                                                  const std::vector<std::string>& data,
                                                  const std::vector<hsize_t>& hs_outer,
                                                  const std::string& default_value)
{
    std::vector<char> data_char;
    std::vector<hsize_t> hs = hs_outer;

    std::vector<std::uint32_t> sizes;
    size_t max_size = 0;
    for (const auto& i : data)
    {
        sizes.push_back(i.size());
        if (i.size() > max_size)
            max_size = i.size();
    }

    char fill_value = '\0';
    for (auto d : data)
    {
        d.resize(max_size, fill_value);
        for (char c : d) data_char.push_back(c);
    }
    hs.push_back(max_size);

    glog.is_debug1() && glog << "Writing string field \"" << dataset_name
                             << "\" (size: " << dim_str(hs) << ")" << std::endl;

    auto maxhs = hs;
    H5::DSetCreatPropList prop;
    H5::Group& grp = group_factory_.fetch_group(group);
    bool ds_exists = grp.exists(dataset_name);
    if (!ds_exists && use_chunks_)
    {
        // all dimensions may change
        for (auto& m : maxhs) m = H5S_UNLIMITED;

        auto chunkhs = hs;
        // message dimension
        chunkhs.front() = chunk_length_;

        // string width dimension
        constexpr hsize_t str_chunk_size = 256;
        chunkhs.back() = str_chunk_size;

        // set all chunk dimensions to at least 1
        for (auto& s : chunkhs)
        {
            if (s == 0)
                s = 1;
        }
        glog.is_debug2() && glog << "Setting chunks to " << dim_str(chunkhs) << std::endl;

        prop.setChunk(chunkhs.size(), chunkhs.data());
        prop.setFillValue(H5::PredType::NATIVE_CHAR, &fill_value);
    }

    std::unique_ptr<H5::DataSpace> dataspace;
    if (data_char.size() || write_zero_length_dim_)
        dataspace = std::make_unique<H5::DataSpace>(hs.size(), hs.data(), maxhs.data());
    else
        dataspace = std::make_unique<H5::DataSpace>(H5S_NULL);

    H5::DataSet dataset =
        ds_exists ? grp.openDataSet(dataset_name)
                  : grp.createDataSet(dataset_name, H5::PredType::NATIVE_CHAR, *dataspace, prop);

    if (ds_exists)
    {
        H5::DataSpace existing_space(dataset.getSpace());
        std::vector<hsize_t> existing_hs(existing_space.getSimpleExtentNdims());
        existing_space.getSimpleExtentDims(&existing_hs[0]);
        glog.is_debug2() && glog << "Existing dimensions are: " << dim_str(existing_hs)
                                 << std::endl;

        std::vector<hsize_t> new_size(hs.size(), 0);
        for (int i = 0, n = new_size.size(); i < n; ++i)
            new_size[i] = std::max(hs[i], existing_hs[i]);

        new_size.front() = hs.front() + existing_hs.front();

        glog.is_debug2() && glog << "Extending dimensions to: " << dim_str(new_size) << std::endl;
        dataset.extend(new_size.data());

        auto& memspace = dataspace;
        H5::DataSpace filespace(dataset.getSpace());
        std::vector<hsize_t> offset(hs.size(), 0);
        offset.front() += existing_hs.front();

        glog.is_debug2() && glog << "Selecting offset of: " << dim_str(offset) << std::endl;

        filespace.selectHyperslab(H5S_SELECT_SET, hs.data(), offset.data());
        if (data_char.size())
            dataset.write(&data_char[0], H5::PredType::NATIVE_CHAR, *memspace, filespace);
    }
    else
    {
        if (data_char.size())
            dataset.write(&data_char[0], H5::PredType::NATIVE_CHAR);
    }

    glog.is_debug1() && glog << "Writing string size field \"" << dataset_name + "_size"
                             << "\" (size: " << dim_str(hs_outer) << ")" << std::endl;

    write_vector(group, dataset_name + "_size", sizes, hs_outer, std::uint32_t(0),
                 static_cast<std::uint32_t>(0) /* use empty value of 0 not uint32 max */);

    const char* default_value_attr_name = "default_value";
    if (!dataset.attrExists(default_value_attr_name))
    {
        const int rank = 1;
        hsize_t att_hs[] = {1};
        H5::DataSpace att_space(rank, att_hs, att_hs);
        H5::StrType att_datatype(H5::PredType::C_S1, default_value.size() + 1);
        H5::Attribute att =
            dataset.createAttribute(default_value_attr_name, att_datatype, att_space);
        const H5std_string& strbuf(default_value);
        att.write(att_datatype, strbuf);
    }
}
