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

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

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

namespace
{
// the tool installed beside this one, as goby's own subtools are found, else from the PATH
std::string sibling_or_path(const std::string& app)
{
    std::error_code ec;
    auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
    {
        auto sibling = self.parent_path() / app;
        if (std::filesystem::exists(sibling, ec))
            return sibling.string();
    }
    return app;
}

void exec(const std::vector<std::string>& args)
{
    std::vector<char*> c_args;
    for (const auto& arg : args) c_args.push_back(const_cast<char*>(arg.c_str()));
    c_args.push_back(nullptr);
    auto path = sibling_or_path(args.front());
    if (path.find('/') != std::string::npos)
        execv(path.c_str(), c_args.data());
    execvp(c_args[0], c_args.data());
}

/// \brief goby launch <deployment>.yml [--platform ...]: writes the platform's goby_launch file
/// beside the deployment file (so preseed.goby and relative paths still work) and runs it.
/// Returns only when no deployment file was given.
void launch_deployment(const std::vector<std::string>& args)
{
    auto is_deployment = [](const std::string& arg)
    {
        auto ext = std::filesystem::path(arg).extension().string();
        return ext == ".yml" || ext == ".yaml";
    };
    auto deployment_it = std::find_if(args.begin(), args.end(), is_deployment);
    if (deployment_it == args.end())
        return;

    static const std::set<std::string> interface_options{
        "--platform", "--instance", "--module", "-D", "--define", "-I", "--interface_dir"};
    std::vector<std::string> generate{"goby_interface_tool", "launch", *deployment_it};
    std::vector<std::string> launch{"goby_launch"};
    std::string unit;
    for (auto it = args.begin(); it != args.end(); ++it)
    {
        if (it == deployment_it)
            continue;
        auto eq = it->find('=');
        auto key = it->substr(0, eq);
        if (!interface_options.count(key))
        {
            launch.push_back(*it);
            continue;
        }
        generate.push_back(*it);
        std::string value = eq == std::string::npos ? "" : it->substr(eq + 1);
        if (eq == std::string::npos && std::next(it) != args.end())
        {
            value = *(++it);
            generate.push_back(value);
        }
        if (key == "--platform" || key == "--instance" || key == "--module")
            unit += (unit.empty() ? "" : ".") + value;
    }

    // one file per platform, so several vehicles can be launched from the same deployment
    std::filesystem::path deployment(*deployment_it);
    auto generated = deployment.parent_path() / ("." + deployment.stem().string() +
                                                 (unit.empty() ? "" : "." + unit) + ".launch");
    generate.push_back("--output_file");
    generate.push_back(generated.string());

    pid_t pid = fork();
    if (pid == 0)
    {
        exec(generate);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        std::exit(WIFEXITED(status) && WEXITSTATUS(status) != 127 ? WEXITSTATUS(status) : 1);

    launch.push_back(generated.string());
    exec(launch);
    std::cerr << "Could not run goby_launch; ensure it is on your PATH" << std::endl;
    std::exit(1);
}
} // namespace

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

    int exit_code = 0;
    if (app_cfg().action() == goby::apps::middleware::protobuf::GobyToolConfig::launch)
    {
        const auto& extra = app_cfg().app().tool_cfg().extra_cli_param();
        launch_deployment(std::vector<std::string>(extra.begin(), extra.end()));
    }

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
                exit_code = tool_helper.run_subtool<goby::apps::middleware::UnifiedLogTool>();
                break;

            case goby::apps::middleware::protobuf::GobyToolConfig::protobuf:
                exit_code = tool_helper.run_subtool<goby::apps::middleware::ProtobufTool>();
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
