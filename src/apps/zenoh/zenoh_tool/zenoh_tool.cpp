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

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/middleware/tool/publish_subscribe_tool.h"
#include "goby/zenoh/protobuf/tool_config.pb.h"
#include "goby/zenoh/transport/interprocess.h"

namespace goby
{
namespace apps
{
namespace zenoh
{
class ZenohToolConfigurator
    : public goby::middleware::ProtobufConfigurator<goby::zenoh::protobuf::ZenohToolConfig>
{
  public:
    ZenohToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<goby::zenoh::protobuf::ZenohToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ZenohTool : public goby::middleware::Application<goby::zenoh::protobuf::ZenohToolConfig>
{
  public:
    ZenohTool();
    ~ZenohTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }
};

using ZenohPublishTool =
    goby::middleware::tool::PublishToolApplication<goby::zenoh::detail::InterProcessTag,
                                                   goby::zenoh::protobuf::PublishToolConfig>;

using ZenohSubscribeTool =
    goby::middleware::tool::SubscribeToolApplication<goby::zenoh::detail::InterProcessTag,
                                                     goby::zenoh::protobuf::SubscribeToolConfig>;

} // namespace zenoh
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zenoh::ZenohTool>(
        goby::apps::zenoh::ZenohToolConfigurator(argc, argv));
}

goby::apps::zenoh::ZenohTool::ZenohTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::zenoh::protobuf::ZenohToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::zenoh::protobuf::ZenohToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::zenoh::protobuf::ZenohToolConfig::publish:
                            tool_helper.help<goby::apps::zenoh::ZenohPublishTool>(action_for_help);
                            break;

                        case goby::zenoh::protobuf::ZenohToolConfig::subscribe:
                            tool_helper.help<goby::apps::zenoh::ZenohSubscribeTool>(
                                action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::zenoh::protobuf::ZenohToolConfig::publish:
                tool_helper.run_subtool<goby::apps::zenoh::ZenohPublishTool>();
                break;

            case goby::zenoh::protobuf::ZenohToolConfig::subscribe:
                tool_helper.run_subtool<goby::apps::zenoh::ZenohSubscribeTool>();
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
