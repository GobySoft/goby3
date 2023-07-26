// Copyright 2016-2022:
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

#ifndef GOBY_MIDDLEWARE_LOG_HDF5_HDF5_H
#define GOBY_MIDDLEWARE_LOG_HDF5_HDF5_H

#include <algorithm> // for max
#include <cstdint>   // for uint64_t
#include <deque>     // for deque
#include <map>       // for map, multimap
#include <memory>    // for shared_ptr
#include <string>    // for string
#include <utility>   // for move
#include <vector>    // for vector

#include <H5Cpp.h>
#include <google/protobuf/descriptor.h> // for FieldDescriptor
#include <google/protobuf/message.h>    // for Message, Reflection

#include "hdf5_predicate.h"       // for predicate
#include "hdf5_protobuf_values.h" // for PBMeta, retrieve_default_value

namespace goby
{
namespace middleware
{
struct HDF5ProtobufEntry;

namespace hdf5
{
struct MessageCollection
{
    MessageCollection(std::string n, std::string parent_group)
        : name(std::move(n)), group(parent_group + "/" + name)
    {
    }
    std::string name;
    std::string group;

    // time -> ProtobufEntry
    std::multimap<std::uint64_t, HDF5ProtobufEntry> entries;
};

struct Channel
{
    Channel(std::string n) : name(std::move(n)), group("/" + name) {}
    std::string name;
    std::string group;

    // returns number of messages in the collection
    size_t add_message(const goby::middleware::HDF5ProtobufEntry& entry);

    // message name -> hdf5::Message
    std::map<std::string, MessageCollection> entries;
};

// keeps track of HDF5 groups for us, and creates them as needed
class GroupFactory
{
  public:
    GroupFactory(H5::H5File& h5file) : root_group_(h5file.openGroup("/")) {}

    // creates or opens group
    H5::Group& fetch_group(const std::string& group_path);

  private:
    class GroupWrapper
    {
      public:
        // for root group (already exists)
        GroupWrapper(const H5::Group& group) : group_(group) {}

        // for children groups
        GroupWrapper(const std::string& name, H5::Group& parent) : group_(parent.createGroup(name))
        {
        }

        H5::Group& fetch_group(std::deque<std::string>& nodes);

      private:
        H5::Group group_;
        std::map<std::string, GroupWrapper> children_;
    };
    GroupWrapper root_group_;
};

class Writer
{
  public:
    Writer(const std::string& output_file, bool write_zero_length_dim = true,
           bool use_chunks = false, hsize_t chunk_length = 0);

    void add_entry(goby::middleware::HDF5ProtobufEntry entry);

    void write();

  private:
    void write_channel(const goby::middleware::hdf5::Channel& channel);
    void
    write_message_collection(const goby::middleware::hdf5::MessageCollection& message_collection);
    void write_time(const std::string& group,
                    const goby::middleware::hdf5::MessageCollection& message_collection);
    void write_scheme(const std::string& group,
                      const goby::middleware::hdf5::MessageCollection& message_collection);

    void write_field_selector(const std::string& group,
                              const google::protobuf::FieldDescriptor* field_desc,
                              const std::vector<const google::protobuf::Message*>& messages,
                              std::vector<hsize_t>& hs);

    void write_enum_attributes(const std::string& group,
                               const google::protobuf::FieldDescriptor* field_desc);

    template <typename T>
    void write_field(const std::string& group, const google::protobuf::FieldDescriptor* field_desc,
                     const std::vector<const google::protobuf::Message*>& messages,
                     std::vector<hsize_t>& hs);

    void write_embedded_message(const std::string& group,
                                const google::protobuf::FieldDescriptor* field_desc,
                                const std::vector<const google::protobuf::Message*> messages,
                                std::vector<hsize_t>& hs);

    template <typename T>
    void write_vector(const std::string& group, const std::string dataset_name,
                      const std::vector<T>& data, const std::vector<hsize_t>& hs,
                      const T& default_value, T empty_value = retrieve_empty_value<T>());

    void write_vector(const std::string& group, const std::string& dataset_name,
                      const std::vector<std::string>& data, const std::vector<hsize_t>& hs,
                      const std::string& default_value);

    std::string dim_str(const std::vector<hsize_t>& hs)
    {
        std::string d;
        for (const auto& h : hs) d += std::to_string(h) + ",";
        d.pop_back();
        return d;
    }

  private:
    // channel name -> hdf5::Channel
    std::map<std::string, goby::middleware::hdf5::Channel> channels_;
    H5::H5File h5file_;
    goby::middleware::hdf5::GroupFactory group_factory_;
    bool write_zero_length_dim_;
    bool use_chunks_;
    hsize_t chunk_length_;
};

template <typename T>
void Writer::write_field(const std::string& group,
                         const google::protobuf::FieldDescriptor* field_desc,
                         const std::vector<const google::protobuf::Message*>& messages,
                         std::vector<hsize_t>& hs)
{
    if (field_desc->is_repeated())
    {
        // pass one to figure out field size
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

        std::vector<T> values(messages.size() * max_field_size, retrieve_empty_value<T>());

        for (unsigned i = 0, n = messages.size(); i < n; ++i)
        {
            if (messages[i])
            {
                const google::protobuf::Reflection* refl = messages[i]->GetReflection();
                int field_size = refl->FieldSize(*messages[i], field_desc);
                for (int j = 0; j < field_size; ++j)
                    retrieve_repeated_value<T>(&values[i * max_field_size + j], j,
                                               PBMeta(refl, field_desc, (*messages[i])));
            }
        }

        T default_value;
        retrieve_default_value(&default_value, field_desc);

        write_vector(group, field_desc->name(), values, hs, default_value);

        hs.pop_back();
    }
    else
    {
        std::vector<T> values(messages.size(), retrieve_empty_value<T>());
        for (unsigned i = 0, n = messages.size(); i < n; ++i)
        {
            if (messages[i])
            {
                const google::protobuf::Reflection* refl = messages[i]->GetReflection();
                retrieve_single_value<T>(&values[i], PBMeta(refl, field_desc, (*messages[i])));
            }
        }

        T default_value;
        retrieve_default_value(&default_value, field_desc);

        write_vector(group, field_desc->name(), values, hs, default_value);
    }
}

template <typename T>
void Writer::write_vector(const std::string& group, const std::string dataset_name,
                          const std::vector<T>& data, const std::vector<hsize_t>& hs,
                          const T& default_value, T empty_value)
{
    std::unique_ptr<H5::DataSpace> dataspace;
    H5::Group& grp = group_factory_.fetch_group(group);

    auto maxhs = hs;
    H5::DSetCreatPropList prop;
    bool ds_exists = grp.exists(dataset_name);
    if (!ds_exists && use_chunks_)
    {
        // all dimensions may change
        for (auto& m : maxhs) m = H5S_UNLIMITED;
        auto chunkhs = hs;
        chunkhs.front() = chunk_length_;

        // set all chunk dimensions to at least 1
        for (auto& s : chunkhs)
        {
            if (s == 0)
                s = 1;
        }

        glog.is_debug2() && glog << "Setting chunks to " << dim_str(chunkhs) << std::endl;
        prop.setChunk(chunkhs.size(), chunkhs.data());
        prop.setFillValue(predicate<T>(), &empty_value);
    }

    if (data.size() || write_zero_length_dim_)
        dataspace = std::make_unique<H5::DataSpace>(hs.size(), hs.data(), maxhs.data());
    else
        dataspace = std::make_unique<H5::DataSpace>(H5S_NULL);

    H5::DataSet dataset = ds_exists
                              ? grp.openDataSet(dataset_name)
                              : grp.createDataSet(dataset_name, predicate<T>(), *dataspace, prop);

    if (ds_exists)
    {
        H5::DataSpace existing_space(dataset.getSpace());
        std::vector<hsize_t> existing_hs(existing_space.getSimpleExtentNdims());
        existing_space.getSimpleExtentDims(&existing_hs[0]);
        glog.is_debug2() && glog << "Existing dimensions are: " << dim_str(existing_hs)
                                 << std::endl;

        // new size is larger of the existing or new vectors, except for the last dimension which is the sum
        std::vector<hsize_t> new_size(hs.size(), 0);
        for (int i = 0, n = new_size.size(); i < n; ++i)
            new_size[i] = std::max(hs[i], existing_hs[i]);
        new_size.front() = existing_hs.front() + hs.front();

        glog.is_debug2() && glog << "Extending dimensions to: " << dim_str(new_size) << std::endl;
        dataset.extend(new_size.data());

        auto& memspace = dataspace;
        H5::DataSpace filespace(dataset.getSpace());
        std::vector<hsize_t> offset(hs.size(), 0);
        offset.front() = existing_hs.front();

        glog.is_debug2() && glog << "Selecting offset of: " << dim_str(offset) << std::endl;

        filespace.selectHyperslab(H5S_SELECT_SET, hs.data(), offset.data());
        if (data.size())
            dataset.write(&data[0], predicate<T>(), *memspace, filespace);
    }
    else
    {
        if (data.size())
            dataset.write(&data[0], predicate<T>());
    }

    const char* default_value_attr_name = "default_value";
    if (!dataset.attrExists(default_value_attr_name))
    {
        const int rank = 1;
        hsize_t att_hs[] = {1};
        H5::DataSpace att_space(rank, att_hs, att_hs);
        H5::Attribute att =
            dataset.createAttribute(default_value_attr_name, predicate<T>(), att_space);
        att.write(predicate<T>(), &default_value);
    }
}
} // namespace hdf5
} // namespace middleware
} // namespace goby

#endif
