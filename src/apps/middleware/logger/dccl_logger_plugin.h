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

#ifndef DCCL_LOGGER_20190123_H
#define DCCL_LOGGER_20190123_H

#include <dccl/codec.h>

#include "protobuf_logger_plugin.h"

namespace goby
{
namespace logger
{
class DCCLPlugin : public ProtobufPluginBase
{
  public:
    void register_write_hooks(std::ofstream& out_log_file) override
    {
        ProtobufPluginBase::register_write_hooks<goby::MarshallingScheme::DCCL>(out_log_file);
    }

  private:
    void parse_message(goby::LogEntry& log_entry, google::protobuf::Message* msg) override
    {
        auto desc = msg->GetDescriptor();
        if (loaded_descriptors_.count(desc) == 0)
        {
            codec_.load(desc);
            loaded_descriptors_.insert(desc);
        }

        const auto& data = log_entry.data();
        codec_.decode(data.begin(), data.end(), msg);
    }

  private:
    dccl::Codec codec_;
    std::set<const google::protobuf::Descriptor*> loaded_descriptors_;
};

} // namespace logger
} // namespace goby

#endif
