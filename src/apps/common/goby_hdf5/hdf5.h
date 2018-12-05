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

#ifndef GOBYHDF520160524H
#define GOBYHDF520160524H

#include <boost/algorithm/string.hpp>

#include "H5Cpp.h"

#include "goby/common/application_base.h"
#include "goby/common/hdf5_plugin.h"
#include "goby/common/protobuf/hdf5.pb.h"
#include "goby/util/binary.h"

#include "hdf5_predicate.h"
#include "hdf5_protobuf_values.h"

namespace goby
{
namespace common
{
namespace hdf5
{
struct MessageCollection
{
    MessageCollection(const std::string& n) : name(n) {}
    std::string name;

    // time -> Message contents
    std::multimap<std::uint64_t, boost::shared_ptr<google::protobuf::Message> > entries;
};

struct Channel
{
    Channel(const std::string& n) : name(n) {}
    std::string name;

    void add_message(const goby::common::HDF5ProtobufEntry& entry);

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
        GroupWrapper(H5::Group group) : group_(group) {}

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

class Writer : public goby::common::ApplicationBase
{
  public:
    Writer(goby::common::protobuf::HDF5Config* cfg);

  private:
    void load();
    void collect();
    void write();
    void write_channel(const std::string& group, const goby::common::hdf5::Channel& channel);
    void write_message_collection(const std::string& group,
                                  const goby::common::hdf5::MessageCollection& message_collection);
    void write_time(const std::string& group,
                    const goby::common::hdf5::MessageCollection& message_collection);

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
                      const T& default_value);

    void write_vector(const std::string& group, const std::string dataset_name,
                      const std::vector<std::string>& data, const std::vector<hsize_t>& hs,
                      const std::string& default_value);

    void iterate() {}

  private:
    goby::common::protobuf::HDF5Config& cfg_;
    boost::shared_ptr<goby::common::HDF5Plugin> plugin_;

    // channel name -> hdf5::Channel
    std::map<std::string, goby::common::hdf5::Channel> channels_;
    H5::H5File h5file_;

    goby::common::hdf5::GroupFactory group_factory_;
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
        for (unsigned i = 0, n = messages.size(); i < n; ++i)
        {
            if (messages[i])
            {
                const google::protobuf::Reflection* refl = messages[i]->GetReflection();
                int field_size = refl->FieldSize(*messages[i], field_desc);
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
                          const T& default_value)
{
    H5::DataSpace dataspace(hs.size(), hs.data(), hs.data());
    H5::Group& grp = group_factory_.fetch_group(group);
    H5::DataSet dataset = grp.createDataSet(dataset_name, predicate<T>(), dataspace);
    if (data.size())
        dataset.write(&data[0], predicate<T>());

    const int rank = 1;
    hsize_t att_hs[] = {1};
    H5::DataSpace att_space(rank, att_hs, att_hs);
    H5::Attribute att = dataset.createAttribute("default_value", predicate<T>(), att_space);
    att.write(predicate<T>(), &default_value);
}
} // namespace hdf5
} // namespace common
} // namespace goby

#endif
