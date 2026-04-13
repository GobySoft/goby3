// Copyright 2024-2026:
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
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/tool_config.pb.h"

namespace goby
{
namespace apps
{
namespace zeromq
{
class ZeroMQToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ZeroMQToolConfig>
{
  public:
    ZeroMQToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ZeroMQToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ZeroMQTool : public goby::middleware::Application<protobuf::ZeroMQToolConfig>
{
  public:
    ZeroMQTool();
    ~ZeroMQTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

class PublishTool : public goby::zeromq::SingleThreadApplication<protobuf::PublishToolConfig>,
                    public goby::middleware::ToolSharedLibraryLoader
{
  public:
    PublishTool();
    ~PublishTool() override {}
    void loop() override;

  private:
};

class SubscribeTool : public goby::zeromq::SingleThreadApplication<protobuf::SubscribeToolConfig>,
                      public goby::middleware::ToolSharedLibraryLoader
{
  public:
    SubscribeTool();
    ~SubscribeTool() override {}

  private:
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>> plugins_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::ZeroMQTool>(
        goby::apps::zeromq::ZeroMQToolConfigurator(argc, argv));
}

goby::apps::zeromq::ZeroMQTool::ZeroMQTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::zeromq::protobuf::ZeroMQToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::publish:
                            tool_helper.help<goby::apps::zeromq::PublishTool>(action_for_help);
                            break;

                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::subscribe:
                            tool_helper.help<goby::apps::zeromq::SubscribeTool>(action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::publish:
                tool_helper.run_subtool<goby::apps::zeromq::PublishTool>();
                break;

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::subscribe:
                tool_helper.run_subtool<goby::apps::zeromq::SubscribeTool>();
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

goby::apps::zeromq::PublishTool::PublishTool()
    : goby::zeromq::SingleThreadApplication<protobuf::PublishToolConfig>(1.0 *
                                                                         boost::units::si::hertz),
      goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())
{
    goby::middleware::tool::publish_tool_impl(interprocess(), cfg(), "goby zeromq publish");
}

void goby::apps::zeromq::PublishTool::loop()
{
    static int i = 0;
    ++i;
    if (i > 1) // exit on second call of loop, plenty of time for publish to go through
        quit(0);
}

goby::apps::zeromq::SubscribeTool::SubscribeTool()
    : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())
{
    goby::middleware::tool::subscribe_tool_impl(interprocess(), cfg(), plugins_,
                                                "goby::zeromq::_internal.*");
}
