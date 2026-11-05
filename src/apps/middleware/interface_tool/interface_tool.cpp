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
#include <sstream>

#include <google/protobuf/util/json_util.h>

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/middleware/tool/interface/render_dot.h"
#include "goby/middleware/tool/interface/validate.h"

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

class InterfaceValidateTool
    : public goby::middleware::Application<protobuf::InterfaceValidateConfig>
{
  private:
    void run() override;
};

} // namespace middleware
} // namespace apps
} // namespace goby

namespace
{
// share/goby/interfaces beside the bin/ this tool runs from, in a build tree or an installation
std::string goby_interfaces_dir()
{
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
        return "";
    return (exe.parent_path().parent_path() / "share" / "goby" / "interfaces").string();
}

template <typename Config> interface::LoadParams load_params(const Config& cfg)
{
    interface::LoadParams params;
    params.interface_dirs.assign(cfg.interface_dir().begin(), cfg.interface_dir().end());
    params.goby_interfaces_dir = goby_interfaces_dir();
    for (const auto& define : cfg.define())
    {
        auto eq = define.find('=');
        if (eq == std::string::npos)
            params.defines[define] = "";
        else
            params.defines[define.substr(0, eq)] = define.substr(eq + 1);
    }
    return params;
}

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
                        case Config::validate:
                            tool_helper.help<InterfaceValidateTool>(action_for_help);
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
    auto deployment = interface::load_deployment(cfg.deployment_file(), issues, params_for_load);
    for (const auto& issue : issues)
    {
        if (issue.severity != interface::Issue::Severity::INFO)
            std::cerr << issue << std::endl;
    }
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

namespace
{
using goby::apps::middleware::protobuf::InterfaceIssue;
using goby::apps::middleware::protobuf::InterfaceValidation;

InterfaceIssue::Severity to_proto(interface::Issue::Severity severity)
{
    switch (severity)
    {
        case interface::Issue::Severity::ERROR: return InterfaceIssue::ERROR;
        case interface::Issue::Severity::WARNING: return InterfaceIssue::WARNING;
        case interface::Issue::Severity::INFO: break;
    }
    return InterfaceIssue::INFO;
}

void to_proto(const interface::Issue& issue, InterfaceIssue& pb)
{
    pb.set_severity(to_proto(issue.severity));
    pb.set_code(issue.code);
    pb.set_message(issue.message);
    if (!issue.location.empty())
        pb.set_location(issue.location);

    const auto& s = issue.subject;
    if (s.application.empty())
        return;
    auto& subject = *pb.mutable_subject();
    subject.set_platform(s.platform);
    if (!s.module.empty())
        subject.set_module(s.module);
    subject.set_application(s.application);
    subject.set_thread(s.thread);
    subject.set_layer(s.layer);
    subject.set_group(s.group);
    subject.set_scheme(s.scheme);
    subject.set_type(s.type);
    if (!s.necessity.empty())
        subject.set_necessity(s.necessity);
}

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

std::string render_json(const InterfaceValidation& validation)
{
    std::string out;
    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = true;
    opts.preserve_proto_field_names = true;
    google::protobuf::util::MessageToJsonString(validation, &out, opts);
    return out;
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
