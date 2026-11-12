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

#ifndef GOBY_APPS_MIDDLEWARE_INTERFACE_TOOL_COMMON_H
#define GOBY_APPS_MIDDLEWARE_INTERFACE_TOOL_COMMON_H

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <google/protobuf/util/json_util.h>

#include "goby/middleware/tool/interface/launch.h"
#include "goby/middleware/tool/interface/model.h"

#include "goby/apps/middleware/interface_tool/interface_tool_config.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
namespace interface_tool
{
namespace interface = goby::middleware::tool::interface;

// share/goby/interfaces beside the bin/ this tool runs from, in a build tree or an installation
inline std::string goby_interfaces_dir()
{
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
        return "";
    return (exe.parent_path().parent_path() / "share" / "goby" / "interfaces").string();
}

template <typename Defines, typename Dirs>
interface::LoadParams load_params(const Defines& defines, const Dirs& interface_dirs)
{
    interface::LoadParams params;
    params.interface_dirs.assign(interface_dirs.begin(), interface_dirs.end());
    params.goby_interfaces_dir = goby_interfaces_dir();
    for (const auto& define : defines)
    {
        auto eq = define.find('=');
        if (eq == std::string::npos)
            params.defines[define] = "";
        else
            params.defines[define.substr(0, eq)] = define.substr(eq + 1);
    }
    return params;
}

template <typename Config> interface::LoadParams load_params(const Config& cfg)
{
    return load_params(cfg.define(), cfg.interface_dir());
}

/// \brief Load a deployment, printing its errors and warnings to stderr
///
/// \return false if the deployment file itself could not be read
inline bool load(const std::string& file, const interface::LoadParams& params,
                 interface::Deployment& deployment, interface::Issues& issues)
{
    deployment = interface::load_deployment(file, issues, params);
    bool readable = true;
    for (const auto& issue : issues)
    {
        if (issue.code == "deployment-missing" || issue.code == "deployment-invalid")
            readable = false;
        if (issue.severity != interface::Issue::Severity::INFO)
            std::cerr << issue << std::endl;
    }
    return readable;
}

inline void write_output(const std::string& out, const std::string& output_file)
{
    if (output_file.empty())
    {
        std::cout << out << std::flush;
    }
    else
    {
        std::ofstream fout(output_file);
        if (!fout.is_open())
        {
            std::cerr << "cannot write " << output_file << ": " << std::strerror(errno)
                      << std::endl;
            std::exit(2);
        }
        fout << out;
    }
}

inline std::string render_json(const google::protobuf::Message& msg)
{
    std::string out;
    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = true;
    opts.preserve_proto_field_names = true;
    google::protobuf::util::MessageToJsonString(msg, &out, opts);
    return out;
}

inline void to_proto(const interface::Issue::Subject& s, protobuf::InterfaceEndpoint& endpoint)
{
    endpoint.set_platform(s.platform);
    if (!s.module.empty())
        endpoint.set_module(s.module);
    endpoint.set_application(s.application);
    endpoint.set_thread(s.thread);
    endpoint.set_layer(s.layer);
    endpoint.set_group(s.group);
    endpoint.set_scheme(s.scheme);
    endpoint.set_type(s.type);
    if (!s.necessity.empty())
        endpoint.set_necessity(s.necessity);
}

inline protobuf::InterfaceIssue::Severity to_proto(interface::Issue::Severity severity)
{
    switch (severity)
    {
        case interface::Issue::Severity::ERROR: return protobuf::InterfaceIssue::ERROR;
        case interface::Issue::Severity::WARNING: return protobuf::InterfaceIssue::WARNING;
        case interface::Issue::Severity::INFO: break;
    }
    return protobuf::InterfaceIssue::INFO;
}

inline void to_proto(const interface::Issue& issue, protobuf::InterfaceIssue& pb)
{
    pb.set_severity(to_proto(issue.severity));
    pb.set_code(issue.code);
    pb.set_message(issue.message);
    if (!issue.location.empty())
        pb.set_location(issue.location);
    if (!issue.subject.application.empty())
        to_proto(issue.subject, *pb.mutable_subject());
}

/// \brief The launch unit picked by --platform (with --instance) and --module, or the only one
template <typename Config>
bool select_unit(const interface::Deployment& deployment, const Config& cfg,
                 interface::LaunchUnit& unit, std::string& error)
{
    auto platform = cfg.platform() + (cfg.has_instance() ? std::to_string(cfg.instance()) : "");
    std::vector<interface::LaunchUnit> matches, all = interface::launch_units(deployment);
    for (const auto& u : all)
    {
        if ((platform.empty() || u.platform->name == platform) &&
            (!cfg.has_module() || u.module == cfg.module()))
            matches.push_back(u);
    }
    if (matches.size() == 1)
    {
        unit = matches.front();
        return true;
    }
    std::string names;
    for (const auto& u : all) names += (names.empty() ? "" : ", ") + u.name();
    error = std::string(matches.empty() ? "nothing matches" : "more than one matches") +
            "; choose one of " + names + " with --platform (and --module)";
    return false;
}

/// \brief "platform/[module/]application"
inline std::string application_path(const protobuf::InterfaceEndpoint& e)
{
    return e.platform() + "/" + (e.has_module() ? e.module() + "/" : "") + e.application();
}

} // namespace interface_tool
} // namespace middleware
} // namespace apps
} // namespace goby

#endif
