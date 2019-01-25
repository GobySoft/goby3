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

#ifndef LOG_PLUGIN_20190123_H
#define LOG_PLUGIN_20190123_H

#include "goby/middleware/log/log_entry.h"
#include "goby/middleware/protobuf/log_tool_config.pb.h"
#include "goby/middleware/serialize_parse.h"

namespace goby
{
namespace log
{

class LogPlugin
{
  public:
    virtual void register_write_hooks(std::ofstream& out_log_file) = 0;
    virtual void register_read_hooks(const std::ifstream& in_log_file) = 0;

    virtual std::string debug_text_message(goby::LogEntry& log_entry)
    {
        throw(log::LogException("DEBUG_TEXT is not supported by the scheme's plugin"));
    }
};

} // namespace log
} // namespace goby

#endif
