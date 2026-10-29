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
#include "goby/middleware/tool/config_file.h"
#include "goby/middleware/tool/publish_subscribe_tool.h"
#include "goby/middleware/tool/pubsub_test.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/tool_config.pb.h"
#include "goby/zeromq/transport/intermodule.h"
#include "goby/zeromq/transport/interprocess.h"

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

class TestToolConfigurator : public goby::middleware::ProtobufConfigurator<protobuf::TestToolConfig>
{
  public:
    TestToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::TestToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        try
        {
            if (cfg.has_test_file())
                goby::middleware::tool::load_config_file(cfg.test_file(), cfg);
        }
        catch (const goby::Exception& e)
        {
            error_ = e.what();
            return;
        }
        if (!cfg.has_test())
        {
            error_ = "no test defined: provide a test file or a 'test' block";
            return;
        }
        if (cfg.endpoint_size() == 0)
        {
            auto& ep = *cfg.add_endpoint();
            ep.set_name("default");
            ep.set_layer(goby::middleware::protobuf::LAYER_INTERPROCESS);
        }
        for (auto& ep : *cfg.mutable_endpoint())
        {
            // endpoint settings layer over the tool-wide --interprocess settings
            goby::zeromq::protobuf::InterProcessPortalConfig merged = cfg.interprocess();
            merged.MergeFrom(ep.interprocess());
            if (!merged.has_client_name())
                merged.set_client_name(cfg.app().name() + "." + ep.name());
            *ep.mutable_interprocess() = merged;
        }
    }

  private:
    // errors are reported from validate() so that goby::run prints them and exits cleanly
    void validate() const override
    {
        ProtobufConfigurator::validate();
        if (!error_.empty())
            throw(goby::middleware::ConfigException(error_));
    }

    std::string error_;
};

class TestTool : public goby::middleware::Application<protobuf::TestToolConfig>,
                 public goby::middleware::ToolSharedLibraryLoader
{
  public:
    TestTool() : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library()) {}
    ~TestTool() override {}

  private:
    void run() override;
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

    int exit_code = 0;
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

                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::test:
                            tool_helper.help<goby::apps::zeromq::TestTool,
                                             goby::apps::zeromq::TestToolConfigurator>(
                                action_for_help);
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

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::test:
                exit_code = tool_helper.run_subtool<goby::apps::zeromq::TestTool,
                                                    goby::apps::zeromq::TestToolConfigurator>();
                break;

            default:
                // perform action will call 'exec' if an external tool performs the action,
                // so if we are continuing, this didn't happen
                throw(goby::Exception("Action was expected to be handled by external tool"));
                break;
        }
    }

    quit(exit_code);
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

void goby::apps::zeromq::TestTool::run()
{
    using goby::middleware::tool::PubSubTestEndpoint;
    using goby::middleware::tool::PubSubTestPortalEndpoint;
    using goby::middleware::tool::PubSubTestRunner;
    using PortalConfig = goby::zeromq::protobuf::InterProcessPortalConfig;

    const auto& cfg = app_cfg();
    int exit_code = 2;
    try
    {
        std::vector<std::unique_ptr<PubSubTestEndpoint>> endpoints;
        for (const auto& ep : cfg.endpoint())
        {
            switch (ep.layer())
            {
                case goby::middleware::protobuf::LAYER_INTERPROCESS:
                    endpoints.push_back(std::make_unique<PubSubTestPortalEndpoint<
                                            goby::zeromq::InterProcessPortal<>, PortalConfig>>(
                        ep.name(), ep.interprocess(), "goby zeromq test"));
                    break;
                case goby::middleware::protobuf::LAYER_INTERMODULE:
                    endpoints.push_back(
                        std::make_unique<PubSubTestPortalEndpoint<goby::zeromq::InterModulePortal<>,
                                                                  PortalConfig>>(
                            ep.name(), ep.interprocess(), "goby zeromq test"));
                    break;
                default:
                    throw goby::Exception("endpoint '" + ep.name() + "': unsupported layer " +
                                          goby::middleware::protobuf::Layer_Name(ep.layer()));
            }
        }

        PubSubTestRunner runner(cfg.test(), std::move(endpoints));
        auto report = runner.run();

        const std::string out = cfg.output_format() == protobuf::TestToolConfig::JSON
                                    ? PubSubTestRunner::render_json(report)
                                    : PubSubTestRunner::render_human(report);
        if (cfg.has_output_file())
        {
            std::ofstream fout(cfg.output_file());
            fout << out;
        }
        else
        {
            std::cout << out << std::flush;
        }

        switch (report.result())
        {
            case goby::middleware::protobuf::PASS: exit_code = 0; break;
            case goby::middleware::protobuf::FAIL: exit_code = 1; break;
            default: exit_code = 2; break;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "goby zeromq test: " << e.what() << std::endl;
    }
    quit(exit_code);
}
