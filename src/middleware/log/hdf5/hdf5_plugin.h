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

#ifndef GOBY_MIDDLEWARE_LOG_HDF5_HDF5_PLUGIN_H
#define GOBY_MIDDLEWARE_LOG_HDF5_HDF5_PLUGIN_H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <memory>

#include "goby/middleware/protobuf/hdf5.pb.h"
#include "goby/time/types.h"
#include "goby/util/primitive_types.h"

namespace goby
{
namespace middleware
{
/// \brief Represents an entry in a HDF5 scientific data file converted from a Google Protocol Buffers message
struct HDF5ProtobufEntry
{
    /// Channel (or Group) name
    std::string channel;
    /// Time of the message
    time::MicroTime time{0 * boost::units::si::seconds};
    /// Actual message contents
    std::shared_ptr<google::protobuf::Message> msg;

    /// Scheme
    int scheme{-1};

    HDF5ProtobufEntry() = default;

    /// Clear the values
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

/// \brief Superclass for implementing plugins for the goby_hdf5 tool for converting from Google Protocol Buffers messages to an HDF5 scientific data file.
///
/// Various plugins can read the Protobuf messages from different formats.
class HDF5Plugin
{
  public:
    HDF5Plugin(const goby::middleware::protobuf::HDF5Config* cfg) {}
    virtual ~HDF5Plugin() = default;

    /// \brief Implement this function in the plugin to provide a single Protobuf message and related metadata to the goby_hdf5 tool.
    ///
    /// \param entry Pointer to HDF5ProtobufEntry that should be populated by the overriding method (in the plugin)
    /// \return true if more data are available, false if no more data are available (end-of-file) or similar. goby_hdf5 will continue to call this method until it returns false.
    virtual bool provide_entry(HDF5ProtobufEntry* entry) = 0;
};
} // namespace middleware
} // namespace goby

#endif
