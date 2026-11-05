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

#include <fstream>
#include <iostream>
#include <sstream>

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/middleware/tool/interface/render_dot.h"

#include "goby/apps/middleware/interface_tool/interface_tool_config.pb.h"

namespace interface = goby::middleware::tool::interface;

namespace goby
{
namespace apps
{
namespace middleware
{
class InterfaceToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::InterfaceToolConfig>
{
  public:
    InterfaceToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::InterfaceToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class InterfaceTool : public goby::middleware::Application<protobuf::InterfaceToolConfig>
{
  public:
    InterfaceTool();

  private:
    void run() override { assert(false); }
};

class InterfaceVisualizeTool
    : public goby::middleware::Application<protobuf::InterfaceVisualizeConfig>
{
  private:
    void run() override;
};

} // namespace middleware
} // namespace apps
} // namespace goby

namespace
{
void write_output(const std::string& out, const std::string& output_file)
{
    if (output_file.empty())
    {
        std::cout << out << std::flush;
    }
    else
    {
        std::ofstream fout(output_file);
        fout << out;
    }
}
} // namespace

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::middleware::InterfaceTool>(
        goby::apps::middleware::InterfaceToolConfigurator(argc, argv));
}

goby::apps::middleware::InterfaceTool::InterfaceTool()
{
    using Config = goby::apps::middleware::protobuf::InterfaceToolConfig;
    goby::middleware::ToolHelper tool_helper(app_cfg().app().binary(), app_cfg().app().tool_cfg(),
                                             Config::Action_descriptor());

    int exit_code = 0;
    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case Config::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case Config::visualize:
                            tool_helper.help<InterfaceVisualizeTool>(action_for_help);
                            break;
                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                    }
                }
                break;
            case Config::visualize:
                exit_code = tool_helper.run_subtool<InterfaceVisualizeTool>();
                break;
            default: throw(goby::Exception("Action was expected to be handled by external tool"));
        }
    }
    quit(exit_code);
}

void goby::apps::middleware::InterfaceVisualizeTool::run()
{
    const auto& cfg = app_cfg();

    interface::Issues issues;
    auto deployment = interface::load_deployment(cfg.deployment_file(), issues);
    for (const auto& issue : issues) std::cerr << issue << std::endl;
    if (interface::has_errors(issues))
    {
        quit(1);
        return;
    }

    interface::DotParams params;
    params.filter.omit_group_regex = cfg.omit_group_regex();
    params.filter.omit_node_regex = cfg.omit_node_regex();
    params.filter.include_coroner = cfg.include_coroner() || cfg.include_all();
    params.filter.include_terminate = cfg.include_terminate() || cfg.include_all();
    params.filter.include_internal = cfg.include_all();
    params.filter.omit_interthread = cfg.omit_interthread();
    params.omit_disconnected = cfg.no_disconnected();
    params.splines = cfg.splines();

    std::stringstream out;
    interface::write_dot(out, deployment, interface::resolve(deployment), params);
    write_output(out.str(), cfg.output_file());
    quit(0);
}
