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
#include "goby/middleware/protobuf/tool_config.pb.h"
#include "goby/middleware/tool/publish_subscribe_tool.h"
#include "goby/udpm/protobuf/tool_config.pb.h"
#include "goby/udpm/transport/interprocess.h"

namespace goby
{
namespace apps
{
namespace udpm
{
class UDPMToolConfigurator
    : public goby::middleware::ProtobufConfigurator<goby::udpm::protobuf::UDPMToolConfig>
{
  public:
    UDPMToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<goby::udpm::protobuf::UDPMToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class UDPMTool : public goby::middleware::Application<goby::udpm::protobuf::UDPMToolConfig>
{
  public:
    UDPMTool();
    ~UDPMTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

using UDPMPublishTool =
    goby::middleware::tool::PublishToolApplication<goby::udpm::detail::InterProcessTag,
                                                   goby::middleware::protobuf::PublishToolConfig>;

using UDPMSubscribeTool = goby::middleware::tool::SubscribeToolApplication<
    goby::udpm::detail::InterProcessTag, goby::middleware::protobuf::SubscribeToolConfig>;

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
        goby::udpm::protobuf::UDPMToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::udpm::protobuf::UDPMToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::udpm::protobuf::UDPMToolConfig::publish:
                            tool_helper.help<goby::apps::udpm::UDPMPublishTool>(action_for_help);
                            break;

                        case goby::udpm::protobuf::UDPMToolConfig::subscribe:
                            tool_helper.help<goby::apps::udpm::UDPMSubscribeTool>(action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::udpm::protobuf::UDPMToolConfig::publish:
                tool_helper.run_subtool<goby::apps::udpm::UDPMPublishTool>();
                break;

            case goby::udpm::protobuf::UDPMToolConfig::subscribe:
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
