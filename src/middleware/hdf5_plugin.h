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

#include <memory>

#include "goby/middleware/protobuf/hdf5.pb.h"
#include "goby/time/typedefs.h"
#include "goby/util/primitive_types.h"

namespace goby
{
namespace middleware
{
struct HDF5ProtobufEntry
{
    std::string channel;
    time::MicroTime time{0 * boost::units::si::seconds};
    std::shared_ptr<google::protobuf::Message> msg;

    HDF5ProtobufEntry() {}

    void clear()
    {
        channel.clear();
        time = time::MicroTime(0 * boost::units::si::seconds);
        msg.reset();
    }
};

inline std::ostream& operator<<(std::ostream& os, const HDF5ProtobufEntry& entry)
{
    os << "@" << entry.time.value() << ": ";
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
    HDF5Plugin(const goby::middleware::protobuf::HDF5Config* cfg) {}
    virtual ~HDF5Plugin() {}

    virtual bool provide_entry(HDF5ProtobufEntry* entry) = 0;
};
} // namespace middleware
} // namespace goby

#endif
