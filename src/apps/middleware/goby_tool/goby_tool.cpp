// Copyright 2024:
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

#include "goby/apps/middleware/goby_tool/log.pb.h"
#include "goby/apps/middleware/goby_tool/tool.pb.h"
#include "marshalling/protobuf.h"
#include "unified_log_tool.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace middleware
{
class GobyToolConfigurator : public goby::middleware::ProtobufConfigurator<protobuf::GobyToolConfig>
{
  public:
    GobyToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::GobyToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class GobyTool : public goby::middleware::Application<protobuf::GobyToolConfig>
{
  public:
    GobyTool();
    ~GobyTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

} // namespace middleware
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::middleware::GobyTool>(
        goby::apps::middleware::GobyToolConfigurator(argc, argv));
}

goby::apps::middleware::GobyTool::GobyTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::middleware::protobuf::GobyToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::middleware::protobuf::GobyToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::middleware::protobuf::GobyToolConfig::log:
                            tool_helper.help<goby::apps::middleware::UnifiedLogTool>(
                                action_for_help);
                            break;
                        case goby::apps::middleware::protobuf::GobyToolConfig::protobuf:
                            tool_helper.help<goby::apps::middleware::ProtobufTool>(action_for_help);
                            break;
                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::middleware::protobuf::GobyToolConfig::log:
                tool_helper.run_subtool<goby::apps::middleware::UnifiedLogTool>();
                break;

            case goby::apps::middleware::protobuf::GobyToolConfig::protobuf:
                tool_helper.run_subtool<goby::apps::middleware::ProtobufTool>();
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
