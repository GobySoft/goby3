// Copyright 2026:
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

#ifndef GOBY_MIDDLEWARE_TOOL_PUBLISH_SUBSCRIBE_TOOL_H
#define GOBY_MIDDLEWARE_TOOL_PUBLISH_SUBSCRIBE_TOOL_H

#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <google/protobuf/text_format.h>

#include "dccl/dynamic_protobuf_manager.h"

#include "goby/middleware/log/dccl_log_plugin.h"
#include "goby/middleware/log/json_log_plugin.h"
#include "goby/middleware/log/log_entry.h"
#include "goby/middleware/log/log_plugin.h"
#include "goby/middleware/log/protobuf_log_plugin.h"
#include "goby/middleware/marshalling/interface.h"
#include "goby/middleware/marshalling/json.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/time/convert.h"
#include "goby/util/debug_logger.h"
#include "goby/util/debug_logger/flex_ostream.h"

namespace goby
{
namespace middleware
{
namespace tool
{

/// \brief Publish a single message on the interprocess layer using the provided publish config.
///
/// \tparam InterprocessType Transport-specific interprocess portal type
/// \tparam PublishConfig Config type with group(), type(), value(), and load_shared_library()
///         accessors (e.g. goby::middleware::protobuf::PublishToolConfig or a superset)
/// \param interprocess Reference to the interprocess portal
/// \param cfg Publish configuration
/// \param tool_name Name of the tool for error messages (e.g. "goby zeromq publish")
template <typename InterprocessType, typename PublishConfig>
void publish_tool_impl(InterprocessType& interprocess, const PublishConfig& cfg,
                       const std::string& tool_name)
{
    using goby::glog;
    std::string type_scheme_str = cfg.type();
    std::string type;
    int scheme{0};

    std::string::size_type slash_pos = type_scheme_str.find('/');
    if (slash_pos == std::string::npos)
    {
        // special cases
        if (type_scheme_str == "JSON")
        {
            scheme = goby::middleware::MarshallingScheme::JSON;
        }
        else if (type_scheme_str.find("protobuf.") != std::string::npos)
        {
            scheme = goby::middleware::MarshallingScheme::PROTOBUF;
            type = type_scheme_str;
        }
    }
    else
    {
        scheme =
            goby::middleware::MarshallingScheme::from_string(type_scheme_str.substr(0, slash_pos));
        type = type_scheme_str.substr(slash_pos + 1);
    }

    goby::middleware::DynamicGroup group(cfg.group());
    switch (scheme)
    {
        case goby::middleware::MarshallingScheme::DCCL:
        case goby::middleware::MarshallingScheme::PROTOBUF:
        {
            // use TextFormat
            auto pb_msg = dccl::DynamicProtobufManager::new_protobuf_message<
                std::shared_ptr<google::protobuf::Message>>(type);
            google::protobuf::TextFormat::Parser parser;
            goby::util::FlexOStreamErrorCollector error_collector(cfg.value());
            parser.RecordErrorsTo(&error_collector);
            parser.AllowPartialMessage(false);
            parser.ParseFromString(cfg.value(), pb_msg.get());

            if (scheme == goby::middleware::MarshallingScheme::DCCL)
                interprocess
                    .template publish_dynamic<google::protobuf::Message,
                                              goby::middleware::MarshallingScheme::DCCL>(pb_msg,
                                                                                         group);
            else if (scheme == goby::middleware::MarshallingScheme::PROTOBUF)
                interprocess
                    .template publish_dynamic<google::protobuf::Message,
                                              goby::middleware::MarshallingScheme::PROTOBUF>(pb_msg,
                                                                                             group);
            break;
        }

        case goby::middleware::MarshallingScheme::JSON:
        {
            auto j = nlohmann::json::parse(cfg.value());
            if (type.empty() || type == "nlohmann::json")
            {
                interprocess.template publish_dynamic<nlohmann::json>(j, group);
            }
            else
            {
                // allow for specialized types, e.g. goby_json_type = "NavigationReport";
                std::vector<char> bytes = goby::middleware::SerializerParserHelper<
                    nlohmann::json, goby::middleware::MarshallingScheme::JSON>::serialize(j);

                interprocess.publish_serialized(type, goby::middleware::MarshallingScheme::JSON,
                                                bytes, group);
            }

            break;
        }

        default:
            glog.is_die() && glog << "Scheme " << scheme << " is not implemented for '" << tool_name
                                  << "'" << std::endl;
    }
}

/// \brief Set up subscriptions for the interprocess subscribe tool.
///
/// \tparam InterprocessType Transport-specific interprocess portal type
/// \tparam SubscribeConfig Config type with group_regex(), type_regex(), scheme(),
///         has_scheme(), include_internal_groups() accessors
/// \param interprocess Reference to the interprocess portal
/// \param cfg Subscribe configuration
/// \param plugins Map of scheme->LogPlugin for deserializing messages
/// \param internal_group_regex Regex pattern for internal groups to filter unless
///        include_internal_groups() is set. Pass empty string to disable filtering.
template <typename InterprocessType, typename SubscribeConfig>
void subscribe_tool_impl(
    InterprocessType& interprocess, const SubscribeConfig& cfg,
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>>& plugins,
    const std::string& internal_group_regex = "")
{
    std::set<int> schemes{goby::middleware::MarshallingScheme::ALL_SCHEMES};

    if (cfg.has_scheme())
    {
        int scheme = goby::middleware::MarshallingScheme::from_string(cfg.scheme());
        schemes = {scheme};
    }

    plugins[goby::middleware::MarshallingScheme::PROTOBUF] =
        std::make_unique<goby::middleware::log::ProtobufPlugin>();
    plugins[goby::middleware::MarshallingScheme::DCCL] =
        std::make_unique<goby::middleware::log::DCCLPlugin>();
    plugins[goby::middleware::MarshallingScheme::JSON] =
        std::make_unique<goby::middleware::log::JSONPlugin>();

    interprocess.subscribe_regex(
        [&cfg, &plugins, internal_group_regex](const std::vector<unsigned char>& bytes, int scheme,
                                               const std::string& type,
                                               const goby::middleware::Group& group)
        {
            if (!internal_group_regex.empty())
            {
                std::regex exclude_pattern(internal_group_regex);
                if (std::regex_match(std::string(group), exclude_pattern) &&
                    !cfg.include_internal_groups())
                    return;
            }

            goby::middleware::log::LogEntry log_entry(bytes, scheme, type, group);
            std::string debug_text;

            auto plugin = plugins.find(log_entry.scheme());
            if (plugin == plugins.end())
            {
                debug_text = std::string("Message of " + std::to_string(bytes.size()) + " bytes");
            }
            else
            {
                try
                {
                    debug_text = plugin->second->debug_text_message(log_entry);
                }
                catch (goby::middleware::log::LogException& e)
                {
                    debug_text = "Unable to parse message of " +
                                 std::to_string(log_entry.data().size()) +
                                 " bytes. Reason: " + e.what();
                }
            }

            // use similar format to goby_log_tool DEBUG_TEXT
            std::cout << scheme << " | " << group << " | " << type << " | "
                      << goby::time::convert<boost::posix_time::ptime>(log_entry.timestamp())
                      << " | " << debug_text << std::endl;
        },
        schemes, cfg.type_regex(), cfg.group_regex());
}

} // namespace tool
} // namespace middleware
} // namespace goby

#endif
