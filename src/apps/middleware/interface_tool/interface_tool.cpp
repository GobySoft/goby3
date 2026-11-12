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

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/middleware/tool/interface/render_d2.h"
#include "goby/middleware/tool/interface/render_dot.h"
#include "goby/middleware/tool/interface/systemd.h"
#include "goby/middleware/tool/interface/validate.h"

#include "goby/apps/middleware/interface_tool/interface_tool_config.pb.h"

#include "common.h"
#include "inspect.h"

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

class InterfaceValidateTool
    : public goby::middleware::Application<protobuf::InterfaceValidateConfig>
{
  private:
    void run() override;
};

class InterfaceListTool : public goby::middleware::Application<protobuf::InterfaceListConfig>
{
  private:
    void run() override;
};

class InterfaceExplainTool : public goby::middleware::Application<protobuf::InterfaceExplainConfig>
{
  private:
    void run() override;
};

class InterfaceDiffTool : public goby::middleware::Application<protobuf::InterfaceDiffConfig>
{
  private:
    void run() override;
};

class InterfaceInitTool : public goby::middleware::Application<protobuf::InterfaceInitConfig>
{
  private:
    void run() override;
};

class InterfaceLaunchTool : public goby::middleware::Application<protobuf::InterfaceLaunchConfig>
{
  private:
    void run() override;
};

class InterfaceLaunchCheckTool
    : public goby::middleware::Application<protobuf::InterfaceLaunchCheckConfig>
{
  private:
    void run() override;
};

class InterfaceSystemdTool : public goby::middleware::Application<protobuf::InterfaceSystemdConfig>
{
  private:
    void run() override;
};

} // namespace middleware
} // namespace apps
} // namespace goby

using namespace goby::apps::middleware::interface_tool;

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
                        case Config::validate:
                            tool_helper.help<InterfaceValidateTool>(action_for_help);
                            break;
                        case Config::list:
                            tool_helper.help<InterfaceListTool>(action_for_help);
                            break;
                        case Config::explain:
                            tool_helper.help<InterfaceExplainTool>(action_for_help);
                            break;
                        case Config::diff:
                            tool_helper.help<InterfaceDiffTool>(action_for_help);
                            break;
                        case Config::init:
                            tool_helper.help<InterfaceInitTool>(action_for_help);
                            break;
                        case Config::launch:
                            tool_helper.help<InterfaceLaunchTool>(action_for_help);
                            break;
                        case Config::launch_check:
                            tool_helper.help<InterfaceLaunchCheckTool>(action_for_help);
                            break;
                        case Config::systemd:
                            tool_helper.help<InterfaceSystemdTool>(action_for_help);
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
            case Config::validate:
                exit_code = tool_helper.run_subtool<InterfaceValidateTool>();
                break;
            case Config::list: exit_code = tool_helper.run_subtool<InterfaceListTool>(); break;
            case Config::explain:
                exit_code = tool_helper.run_subtool<InterfaceExplainTool>();
                break;
            case Config::diff: exit_code = tool_helper.run_subtool<InterfaceDiffTool>(); break;
            case Config::init: exit_code = tool_helper.run_subtool<InterfaceInitTool>(); break;
            case Config::launch: exit_code = tool_helper.run_subtool<InterfaceLaunchTool>(); break;
            case Config::launch_check:
                exit_code = tool_helper.run_subtool<InterfaceLaunchCheckTool>();
                break;
            case Config::systemd:
                exit_code = tool_helper.run_subtool<InterfaceSystemdTool>();
                break;
            default: throw(goby::Exception("Action was expected to be handled by external tool"));
        }
    }
    quit(exit_code);
}

void goby::apps::middleware::InterfaceVisualizeTool::run()
{
    const auto& cfg = app_cfg();

    auto params_for_load = load_params(cfg);
    params_for_load.expand_instances = cfg.expand_instances();
    interface::Issues issues;
    interface::Deployment deployment;
    load(cfg.deployment_file(), params_for_load, deployment, issues);
    if (interface::has_errors(issues))
    {
        quit(1);
        return;
    }

    interface::ViewFilter::Params filter;
    filter.omit_group_regex = cfg.omit_group_regex();
    filter.omit_node_regex = cfg.omit_node_regex();
    filter.include_coroner = cfg.include_coroner() || cfg.include_all();
    filter.include_terminate = cfg.include_terminate() || cfg.include_all();
    filter.include_internal = cfg.include_all();
    filter.omit_interthread = cfg.omit_interthread();
    filter.expand_goby_apps = cfg.expand_goby_apps() || cfg.include_all();

    std::stringstream out;
    auto resolution = interface::resolve(deployment);
    if (cfg.format() == protobuf::InterfaceVisualizeConfig::D2)
    {
        interface::D2Params params;
        params.filter = filter;
        params.omit_disconnected = cfg.no_disconnected();
        interface::write_d2(out, deployment, resolution, params);
    }
    else
    {
        interface::DotParams params;
        params.filter = filter;
        params.omit_disconnected = cfg.no_disconnected();
        params.splines = cfg.splines();
        interface::write_dot(out, deployment, resolution, params);
    }
    write_output(out.str(), cfg.output_file());
    quit(0);
}

namespace
{
using goby::apps::middleware::protobuf::InterfaceIssue;
using goby::apps::middleware::protobuf::InterfaceValidation;

std::string severity_name(InterfaceIssue::Severity severity)
{
    switch (severity)
    {
        case InterfaceIssue::ERROR: return "error";
        case InterfaceIssue::WARNING: return "warning";
        case InterfaceIssue::INFO: break;
    }
    return "info";
}

std::string render_human(const InterfaceValidation& validation)
{
    std::map<InterfaceIssue::Severity, int> counts;
    for (const auto& issue : validation.issue()) ++counts[issue.severity()];

    auto plural = [](int n, const std::string& word)
    { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); };

    std::stringstream ss;
    ss << validation.file() << ": " << (validation.valid() ? "valid" : "invalid");
    std::vector<std::string> parts;
    if (counts[InterfaceIssue::ERROR])
        parts.push_back(plural(counts[InterfaceIssue::ERROR], "error"));
    if (counts[InterfaceIssue::WARNING])
        parts.push_back(plural(counts[InterfaceIssue::WARNING], "warning"));
    if (counts[InterfaceIssue::INFO])
        parts.push_back(std::to_string(counts[InterfaceIssue::INFO]) + " info");
    for (std::size_t i = 0; i < parts.size(); ++i) ss << ", " << parts[i];

    const auto& sum = validation.summary();
    ss << " (" << plural(sum.platforms(), "platform") << ", "
       << plural(sum.applications(), "application") << ", "
       << plural(sum.connections(), "connection") << ", " << sum.required_met() << "/"
       << sum.required() << " required met)\n";

    for (const auto& issue : validation.issue())
    {
        ss << "  " << std::left << std::setw(9) << severity_name(issue.severity()) << std::setw(22)
           << issue.code() << " ";
        if (issue.has_subject())
        {
            const auto& s = issue.subject();
            ss << s.platform() << "/" << (s.has_module() ? s.module() + "/" : "") << s.application()
               << " [" << s.layer() << "] ";
        }
        else if (issue.has_location())
        {
            ss << issue.location() << ": ";
        }
        ss << issue.message() << "\n";
    }
    return ss.str();
}

} // namespace

void goby::apps::middleware::InterfaceValidateTool::run()
{
    const auto& cfg = app_cfg();

    interface::Issues issues;
    auto deployment = interface::load_deployment(cfg.deployment_file(), issues, load_params(cfg));
    bool unreadable =
        std::any_of(issues.begin(), issues.end(), [](const interface::Issue& i)
                    { return i.code == "deployment-missing" || i.code == "deployment-invalid"; });

    interface::ValidateParams params;
    params.filter.omit_group_regex = cfg.omit_group_regex();
    params.filter.omit_node_regex = cfg.omit_node_regex();
    params.report_unused = cfg.report_unused();
    params.assume_dynamic = cfg.assume_dynamic();
    auto summary = interface::validate(deployment, interface::resolve(deployment), params, issues);
    interface::check_launch(deployment, issues);

    InterfaceValidation validation;
    validation.set_file(cfg.deployment_file());
    bool valid = true;
    for (const auto& issue : issues)
    {
        to_proto(issue, *validation.add_issue());
        if (issue.severity == interface::Issue::Severity::ERROR ||
            (cfg.strict() && issue.severity == interface::Issue::Severity::WARNING))
            valid = false;
    }
    validation.set_valid(valid);
    auto& sum = *validation.mutable_summary();
    sum.set_platforms(summary.platforms);
    sum.set_applications(summary.applications);
    sum.set_connections(summary.connections);
    sum.set_required(summary.required);
    sum.set_required_met(summary.required_met);

    write_output(cfg.output_format() == protobuf::InterfaceValidateConfig::JSON
                     ? render_json(validation)
                     : render_human(validation),
                 cfg.output_file());

    quit(unreadable ? 2 : (valid ? 0 : 1));
}

void goby::apps::middleware::InterfaceListTool::run()
{
    const auto& cfg = app_cfg();
    interface::Issues issues;
    interface::Deployment deployment;
    if (!load(cfg.deployment_file(), load_params(cfg), deployment, issues))
    {
        quit(2);
        return;
    }

    auto result = interface_tool::list(cfg.deployment_file(), deployment);
    write_output(cfg.output_format() == protobuf::InterfaceValidateConfig::JSON
                     ? render_json(result)
                     : render_human(result, cfg.what()),
                 cfg.output_file());
    quit(0);
}

void goby::apps::middleware::InterfaceExplainTool::run()
{
    const auto& cfg = app_cfg();
    interface::Issues issues;
    interface::Deployment deployment;
    if (!load(cfg.deployment_file(), load_params(cfg), deployment, issues))
    {
        quit(2);
        return;
    }

    protobuf::InterfaceExplanation result;
    try
    {
        result = interface_tool::explain(cfg.deployment_file(), deployment,
                                         interface::resolve(deployment), cfg.group_regex(),
                                         cfg.type_regex());
    }
    catch (const std::regex_error& e)
    {
        std::cerr << "invalid regex: " << e.what() << std::endl;
        quit(2);
        return;
    }
    write_output(cfg.output_format() == protobuf::InterfaceValidateConfig::JSON
                     ? render_json(result)
                     : render_human(result),
                 cfg.output_file());
    // so that a script can tell a pattern that matched nothing
    quit(result.subscription_size() || result.publication_size() ? 0 : 1);
}

void goby::apps::middleware::InterfaceDiffTool::run()
{
    const auto& cfg = app_cfg();
    auto dirs = [&](const google::protobuf::RepeatedPtrField<std::string>& own)
    {
        std::vector<std::string> all(own.begin(), own.end());
        all.insert(all.end(), cfg.interface_dir().begin(), cfg.interface_dir().end());
        return all;
    };
    const auto& file_b =
        cfg.has_deployment_file_b() ? cfg.deployment_file_b() : cfg.deployment_file();

    interface::Issues issues_a, issues_b;
    interface::Deployment a, b;
    bool readable_a = load(cfg.deployment_file(),
                           load_params(cfg.define(), dirs(cfg.interface_dir_a())), a, issues_a);
    bool readable_b =
        load(file_b, load_params(cfg.define(), dirs(cfg.interface_dir_b())), b, issues_b);
    if (!readable_a || !readable_b)
    {
        quit(2);
        return;
    }

    auto result = interface_tool::diff(cfg.deployment_file(), a, file_b, b);
    write_output(cfg.output_format() == protobuf::InterfaceValidateConfig::JSON
                     ? render_json(result)
                     : render_human(result),
                 cfg.output_file());
    // as diff(1): 0 when the same, 1 when different
    quit(has_differences(result) ? 1 : 0);
}

void goby::apps::middleware::InterfaceInitTool::run()
{
    const auto& cfg = app_cfg();
    int applications = 0;
    auto deployment =
        interface_tool::init(cfg.directory(), cfg.platform(), cfg.deployment_name(), applications);
    if (applications == 0)
    {
        std::cerr << "no interface files (with an application: name) found in " << cfg.directory()
                  << std::endl;
        quit(1);
        return;
    }
    write_output(deployment, cfg.output_file());
    quit(0);
}

void goby::apps::middleware::InterfaceLaunchTool::run()
{
    const auto& cfg = app_cfg();
    interface::Issues issues;
    interface::Deployment deployment;
    if (!load(cfg.deployment_file(), load_params(cfg), deployment, issues))
    {
        quit(2);
        return;
    }
    interface::Issues launch_issues;
    interface::check_launch(deployment, launch_issues);
    for (const auto& issue : launch_issues) std::cerr << issue << std::endl;
    if (interface::has_errors(issues) || interface::has_errors(launch_issues))
    {
        quit(1);
        return;
    }

    if (cfg.has_outdir())
    {
        std::filesystem::create_directories(cfg.outdir());
        for (const auto& unit : interface::launch_units(deployment))
        {
            auto path = std::filesystem::path(cfg.outdir()) / (unit.name() + ".launch");
            write_output(interface::render_launch(unit, cfg.deployment_file()), path.string());
            std::filesystem::permissions(path,
                                         std::filesystem::perms::owner_exec |
                                             std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add);
            std::cout << path.string() << std::endl;
        }
        quit(0);
        return;
    }

    interface::LaunchUnit unit;
    std::string error;
    if (!select_unit(deployment, cfg, unit, error))
    {
        std::cerr << error << std::endl;
        quit(1);
        return;
    }
    write_output(interface::render_launch(unit, cfg.deployment_file()), cfg.output_file());
    quit(0);
}

void goby::apps::middleware::InterfaceLaunchCheckTool::run()
{
    const auto& cfg = app_cfg();
    interface::Issues issues;
    interface::Deployment deployment;
    std::ifstream launch_in(cfg.launch_file());
    if (!load(cfg.deployment_file(), load_params(cfg), deployment, issues) || !launch_in.is_open())
    {
        if (!launch_in.is_open())
            std::cerr << "cannot open " << cfg.launch_file() << std::endl;
        quit(2);
        return;
    }
    std::stringstream launch_text;
    launch_text << launch_in.rdbuf();

    interface::LaunchUnit unit;
    std::string error;
    if (!select_unit(deployment, cfg, unit, error))
    {
        std::cerr << error << std::endl;
        quit(2);
        return;
    }

    protobuf::InterfaceLaunchCheck result;
    result.set_launch_file(cfg.launch_file());
    result.set_deployment_file(cfg.deployment_file());
    result.set_unit(unit.name());
    for (const auto& d : interface::check_launch_file(unit, launch_text.str()))
    {
        auto& pb = *result.add_difference();
        pb.set_kind(d.kind);
        pb.set_name(d.name);
        if (d.line_number)
            pb.set_line(d.line_number);
        pb.set_detail(d.detail);
    }

    std::stringstream ss;
    if (cfg.output_format() == protobuf::InterfaceValidateConfig::JSON)
    {
        ss << render_json(result);
    }
    else
    {
        ss << result.launch_file() << " vs " << result.deployment_file() << " (" << result.unit()
           << "): ";
        if (result.difference_size() == 0)
            ss << "no differences\n";
        else
            ss << result.difference_size()
               << (result.difference_size() == 1 ? " difference\n" : " differences\n");
        std::size_t name_width = 0;
        for (const auto& d : result.difference())
            name_width = std::max(name_width, d.name().size());
        for (const auto& d : result.difference())
            ss << "  " << std::left << std::setw(15) << d.kind()
               << std::setw(static_cast<int>(name_width) + 2) << d.name() << d.detail() << "\n";
    }
    write_output(ss.str(), cfg.output_file());
    // as diff(1): 0 when the same, 1 when different
    quit(result.difference_size() ? 1 : 0);
}

void goby::apps::middleware::InterfaceSystemdTool::run()
{
    const auto& cfg = app_cfg();
    interface::Issues issues;
    interface::Deployment deployment;
    if (!load(cfg.deployment_file(), load_params(cfg), deployment, issues))
    {
        quit(2);
        return;
    }

    // one unit unless --outdir is writing them all (each into a directory of its own, since
    // instances share service names)
    std::vector<interface::LaunchUnit> units;
    bool all = cfg.has_outdir() && !cfg.check() && !cfg.has_platform() && !cfg.has_module();
    if (all)
    {
        units = interface::launch_units(deployment);
    }
    else
    {
        interface::LaunchUnit unit;
        std::string error;
        if (!select_unit(deployment, cfg, unit, error))
        {
            std::cerr << error << std::endl;
            quit(1);
            return;
        }
        units.push_back(unit);
    }

    interface::Issues render_issues;
    std::vector<std::pair<std::string, std::vector<interface::SystemdUnitFile>>> rendered;
    for (const auto& unit : units)
        rendered.emplace_back(unit.name(),
                              interface::render_systemd(deployment, unit, render_issues));
    for (const auto& issue : render_issues)
    {
        if (issue.severity != interface::Issue::Severity::INFO)
            std::cerr << issue << std::endl;
    }
    if (interface::has_errors(issues) || interface::has_errors(render_issues))
    {
        quit(1);
        return;
    }

    if (cfg.check())
    {
        int differences = 0;
        for (const auto& file : rendered.front().second)
        {
            auto installed = std::filesystem::path(cfg.unit_dir()) / file.file;
            std::ifstream in(installed);
            std::stringstream text;
            if (in.is_open())
                text << in.rdbuf();
            if (!in.is_open())
                std::cout << "  missing    " << installed.string() << "\n";
            else if (text.str() != file.text)
                std::cout << "  differs    " << installed.string() << "\n";
            else
                continue;
            ++differences;
        }
        std::cout << rendered.front().first << " vs " << cfg.unit_dir() << ": "
                  << (differences ? std::to_string(differences) + " unit(s) differ"
                                  : "no differences")
                  << std::endl;
        quit(differences ? 1 : 0);
        return;
    }

    if (cfg.has_outdir())
    {
        for (const auto& unit : rendered)
        {
            auto dir = std::filesystem::path(cfg.outdir()) / (all ? unit.first : "");
            std::filesystem::create_directories(dir);
            for (const auto& file : unit.second)
            {
                write_output(file.text, (dir / file.file).string());
                std::cout << (dir / file.file).string() << std::endl;
            }
        }
        quit(0);
        return;
    }

    std::stringstream out;
    for (const auto& file : rendered.front().second)
        out << "# --- " << file.file << " ---\n" << file.text << "\n";
    write_output(out.str(), "");
    quit(0);
}
