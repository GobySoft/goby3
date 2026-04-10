// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/marshalling/json.h"
#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/log/dccl_log_plugin.h"
#include "goby/middleware/log/json_log_plugin.h"
#include "goby/middleware/log/protobuf_log_plugin.h"

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/udpm/application/single_thread.h"
#include "goby/apps/udpm/udpm_tool/tool_config.pb.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace udpm
{
class UDPMToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::UDPMToolConfig>
{
  public:
    UDPMToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::UDPMToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class UDPMTool : public goby::middleware::Application<protobuf::UDPMToolConfig>
{
  public:
    UDPMTool();
    ~UDPMTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

class UDPMPublishTool
    : public goby::udpm::SingleThreadApplication<protobuf::UDPMPublishToolConfig>,
      public goby::middleware::ToolSharedLibraryLoader
{
  public:
    UDPMPublishTool();
    ~UDPMPublishTool() override {}
    void loop() override;

  private:
};

class UDPMSubscribeTool
    : public goby::udpm::SingleThreadApplication<protobuf::UDPMSubscribeToolConfig>,
      public goby::middleware::ToolSharedLibraryLoader
{
  public:
    UDPMSubscribeTool();
    ~UDPMSubscribeTool() override {}

  private:
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>> plugins_;
};

} // namespace udpm
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::udpm::UDPMTool>(
        goby::apps::udpm::UDPMToolConfigurator(argc, argv));
}

goby::apps::udpm::UDPMTool::UDPMTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::udpm::protobuf::UDPMToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::udpm::protobuf::UDPMToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::udpm::protobuf::UDPMToolConfig::publish:
                            tool_helper.help<goby::apps::udpm::UDPMPublishTool>(action_for_help);
                            break;

                        case goby::apps::udpm::protobuf::UDPMToolConfig::subscribe:
                            tool_helper.help<goby::apps::udpm::UDPMSubscribeTool>(action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::udpm::protobuf::UDPMToolConfig::publish:
                tool_helper.run_subtool<goby::apps::udpm::UDPMPublishTool>();
                break;

            case goby::apps::udpm::protobuf::UDPMToolConfig::subscribe:
                tool_helper.run_subtool<goby::apps::udpm::UDPMSubscribeTool>();
                break;

            default:
                // perform action will call 'exec' if an external tool performs the action,
                // so if we are continuing, this didn't happen
                throw(goby::Exception("Action was expected to be handled by external tool"));
                break;
        }
    }

    quit(0);
}

goby::apps::udpm::UDPMPublishTool::UDPMPublishTool()
    : goby::udpm::SingleThreadApplication<protobuf::UDPMPublishToolConfig>(1.0 *
                                                                           boost::units::si::hertz),
      goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())

{
    std::string type_scheme_str = cfg().type();
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

    goby::middleware::DynamicGroup group(cfg().group());
    switch (scheme)
    {
        case goby::middleware::MarshallingScheme::DCCL:
        case goby::middleware::MarshallingScheme::PROTOBUF:
        {
            // use TextFormat
            auto pb_msg = dccl::DynamicProtobufManager::new_protobuf_message<
                std::shared_ptr<google::protobuf::Message>>(type);
            google::protobuf::TextFormat::Parser parser;
            goby::util::FlexOStreamErrorCollector error_collector(cfg().value());
            parser.RecordErrorsTo(&error_collector);
            parser.AllowPartialMessage(false);
            parser.ParseFromString(cfg().value(), pb_msg.get());

            if (scheme == goby::middleware::MarshallingScheme::DCCL)
                interprocess()
                    .publish_dynamic<google::protobuf::Message,
                                     goby::middleware::MarshallingScheme::DCCL>(pb_msg, group);
            else if (scheme == goby::middleware::MarshallingScheme::PROTOBUF)
                interprocess()
                    .publish_dynamic<google::protobuf::Message,
                                     goby::middleware::MarshallingScheme::PROTOBUF>(pb_msg, group);
            break;
        }

        case goby::middleware::MarshallingScheme::JSON:
        {
            auto j = nlohmann::json::parse(cfg().value());
            if (type.empty() || type == "nlohmann::json")
            {
                interprocess().publish_dynamic<nlohmann::json>(j, group);
            }
            else
            {
                // allow for specialized types, e.g. goby_json_type = "NavigationReport";
                std::vector<char> bytes = goby::middleware::SerializerParserHelper<
                    nlohmann::json, goby::middleware::MarshallingScheme::JSON>::serialize(j);

                interprocess().publish_serialized(type, goby::middleware::MarshallingScheme::JSON,
                                                  bytes, group);
            }

            break;
        }

        default:
            glog.is_die() && glog << "Scheme " << scheme
                                  << " is not implemented for 'goby udpm publish'" << std::endl;
    }
}

void goby::apps::udpm::UDPMPublishTool::loop()
{
    static int i = 0;
    ++i;
    if (i > 1) // exit on second call of loop, plenty of time for publish to go through
        quit(0);
}

goby::apps::udpm::UDPMSubscribeTool::UDPMSubscribeTool()
    : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())

{
    std::set<int> schemes{goby::middleware::MarshallingScheme::ALL_SCHEMES};

    if (cfg().has_scheme())
    {
        int scheme = goby::middleware::MarshallingScheme::from_string(cfg().scheme());
        schemes = {scheme};
    }

    plugins_[goby::middleware::MarshallingScheme::PROTOBUF] =
        std::make_unique<goby::middleware::log::ProtobufPlugin>();
    plugins_[goby::middleware::MarshallingScheme::DCCL] =
        std::make_unique<goby::middleware::log::DCCLPlugin>();
    plugins_[goby::middleware::MarshallingScheme::JSON] =
        std::make_unique<goby::middleware::log::JSONPlugin>();

    interprocess().subscribe_regex(
        [this](const std::vector<unsigned char>& bytes, int scheme, const std::string& type,
               const goby::middleware::Group& group)
        {
            goby::middleware::log::LogEntry log_entry(bytes, scheme, type, group);
            std::string debug_text;

            auto plugin = plugins_.find(log_entry.scheme());
            if (plugin == plugins_.end())
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
        schemes, cfg().type_regex(), cfg().group_regex());
}
