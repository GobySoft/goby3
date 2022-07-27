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

#ifndef GOBY_MIDDLEWARE_LOG_JSON_LOG_PLUGIN_H
#define GOBY_MIDDLEWARE_LOG_JSON_LOG_PLUGIN_H

#include "goby/middleware/log.h"
#include "goby/middleware/marshalling/json.h"
#include "goby/middleware/protobuf/log_tool_config.pb.h"
#include "goby/time/convert.h"
#include "log_plugin.h"

namespace goby
{
namespace middleware
{
namespace log
{
class JSONPlugin : public LogPlugin
{
  public:
    std::string debug_text_message(LogEntry& log_entry) override
    {
        return parse_message(log_entry)->dump();
    }

    std::shared_ptr<nlohmann::json> json_message(LogEntry& log_entry) override
    {
        return parse_message(log_entry);
    }

    void register_read_hooks(const std::ifstream& in_log_file) override {}

    void register_write_hooks(std::ofstream& out_log_file) override {}

    std::shared_ptr<nlohmann::json> parse_message(LogEntry& log_entry)
    {
        const auto& data = log_entry.data();
        auto bytes_begin = data.begin(), bytes_end = data.end(), actual_end = data.begin();
        return SerializerParserHelper<nlohmann::json,
                                      goby::middleware::MarshallingScheme::JSON>::parse(bytes_begin,
                                                                                        bytes_end,
                                                                                        actual_end);
    }
};

} // namespace log
} // namespace middleware
} // namespace goby

#endif
