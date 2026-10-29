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
#include "goby/middleware/tool/config_file.h"
#include "goby/middleware/tool/publish_subscribe_tool.h"
#include "goby/middleware/tool/pubsub_test.h"
#include "goby/udpm/application/single_thread.h"
#include "goby/udpm/protobuf/tool_config.pb.h"
#include "goby/udpm/transport/intermodule.h"
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

class UDPMPublishTool
    : public goby::udpm::SingleThreadApplication<goby::middleware::protobuf::PublishToolConfig>,
      public goby::middleware::ToolSharedLibraryLoader
{
  public:
    UDPMPublishTool();
    ~UDPMPublishTool() override {}
    void loop() override;

  private:
};

class UDPMSubscribeTool
    : public goby::udpm::SingleThreadApplication<goby::middleware::protobuf::SubscribeToolConfig>,
      public goby::middleware::ToolSharedLibraryLoader
{
  public:
    UDPMSubscribeTool();
    ~UDPMSubscribeTool() override {}

  private:
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>> plugins_;
};

class UDPMTestToolConfigurator
    : public goby::middleware::ProtobufConfigurator<goby::middleware::protobuf::TestToolConfig>
{
  public:
    UDPMTestToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<goby::middleware::protobuf::TestToolConfig>(argc,
                                                                                             argv)
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
            goby::udpm::protobuf::InterProcessPortalConfig merged = cfg.interprocess();
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

class UDPMTestTool
    : public goby::middleware::Application<goby::middleware::protobuf::TestToolConfig>,
      public goby::middleware::ToolSharedLibraryLoader
{
  public:
    UDPMTestTool() : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library()) {}
    ~UDPMTestTool() override {}

  private:
    void run() override;
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
        goby::udpm::protobuf::UDPMToolConfig::Action_descriptor());

    int exit_code = 0;
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

                        case goby::udpm::protobuf::UDPMToolConfig::test:
                            tool_helper.help<goby::apps::udpm::UDPMTestTool,
                                             goby::apps::udpm::UDPMTestToolConfigurator>(
                                action_for_help);
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

            case goby::udpm::protobuf::UDPMToolConfig::test:
                exit_code = tool_helper.run_subtool<goby::apps::udpm::UDPMTestTool,
                                                    goby::apps::udpm::UDPMTestToolConfigurator>();
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

goby::apps::udpm::UDPMPublishTool::UDPMPublishTool()
    : goby::udpm::SingleThreadApplication<goby::middleware::protobuf::PublishToolConfig>(
          1.0 * boost::units::si::hertz),
      goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())
{
    goby::middleware::tool::publish_tool_impl(interprocess(), cfg(), "goby udpm publish");
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
    goby::middleware::tool::subscribe_tool_impl(interprocess(), cfg(), plugins_);
}

void goby::apps::udpm::UDPMTestTool::run()
{
    using goby::middleware::tool::PubSubTestEndpoint;
    using goby::middleware::tool::PubSubTestPortalEndpoint;
    using goby::middleware::tool::PubSubTestRunner;
    using PortalConfig = goby::udpm::protobuf::InterProcessPortalConfig;
    using TestToolConfig = goby::middleware::protobuf::TestToolConfig;

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
                    endpoints.push_back(
                        std::make_unique<PubSubTestPortalEndpoint<goby::udpm::InterProcessPortal<>,
                                                                  PortalConfig>>(
                            ep.name(), ep.interprocess(), "goby udpm test"));
                    break;
                case goby::middleware::protobuf::LAYER_INTERMODULE:
                    endpoints.push_back(
                        std::make_unique<PubSubTestPortalEndpoint<goby::udpm::InterModulePortal<>,
                                                                  PortalConfig>>(
                            ep.name(), ep.interprocess(), "goby udpm test"));
                    break;
                default:
                    throw goby::Exception("endpoint '" + ep.name() + "': unsupported layer " +
                                          goby::middleware::protobuf::Layer_Name(ep.layer()));
            }
        }

        PubSubTestRunner runner(cfg.test(), std::move(endpoints));
        auto report = runner.run();

        const std::string out = cfg.output_format() == TestToolConfig::JSON
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
        std::cerr << "goby udpm test: " << e.what() << std::endl;
    }
    quit(exit_code);
}
