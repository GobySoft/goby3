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

#ifndef HDF5_PLUGIN20160523H
#define HDF5_PLUGIN20160523H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <boost/shared_ptr.hpp>

#include "goby/common/protobuf/hdf5.pb.h"
#include "goby/util/primitive_types.h"

namespace goby
{
namespace common
{
struct HDF5ProtobufEntry
{
    std::string channel;
    std::uint64_t time;
    boost::shared_ptr<google::protobuf::Message> msg;

    HDF5ProtobufEntry() : time(0) {}

    void clear()
    {
        channel.clear();
        time = 0;
        msg.reset();
    }
};

inline std::ostream& operator<<(std::ostream& os, const HDF5ProtobufEntry& entry)
{
    os << "@" << entry.time << ": ";
    os << "/" << entry.channel;
    if (entry.msg)
    {
        os << "/" << entry.msg->GetDescriptor()->full_name();
        os << " " << entry.msg->ShortDebugString();
    }
    return os;
}

class HDF5Plugin
{
  public:
    HDF5Plugin(goby::common::protobuf::HDF5Config* cfg) {}
    virtual ~HDF5Plugin() {}

    virtual bool provide_entry(HDF5ProtobufEntry* entry) = 0;
};
} // namespace common
} // namespace goby

#endif
