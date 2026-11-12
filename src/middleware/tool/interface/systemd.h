// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_SYSTEMD_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_SYSTEMD_H

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "goby/middleware/tool/interface/launch.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
struct SystemdUnitFile
{
    std::string file;
    std::string text;
};

namespace detail
{
// systemd unit names: letters, digits and ":_.-@"
inline std::string unit_name(const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
        out += (std::isalnum(c) || std::string(":_.-@").find(c) != std::string::npos) ? c : '_';
    return out;
}

/// \brief A when: condition decided now, from the values -D and the environment gave the
/// deployment; nullopt when it cannot be (a value left for the shell, or a command)
inline std::optional<bool> evaluate_when(const std::string& when)
{
    for (const std::string op : {" =~ ", " == ", " != "})
    {
        auto pos = when.find(op);
        if (pos == std::string::npos)
            continue;
        auto lhs = when.substr(0, pos), rhs = when.substr(pos + op.size());
        if ((lhs + rhs).find("${") != std::string::npos)
            return std::nullopt;
        if (op == " =~ ")
            return std::regex_search(lhs, std::regex(rhs, std::regex::extended));
        return (lhs == rhs) == (op == " == ");
    }
    return std::nullopt;
}

inline SystemdSettings merged(const SystemdSettings& defaults, const SystemdSettings& own)
{
    SystemdSettings s = own;
    auto fill = [](std::optional<std::string>& field, const std::optional<std::string>& fallback)
    {
        if (!field)
            field = fallback;
    };
    fill(s.unit_template, defaults.unit_template);
    fill(s.user, defaults.user);
    fill(s.group, defaults.group);
    fill(s.wanted_by, defaults.wanted_by);
    fill(s.description, defaults.description);
    fill(s.condition, defaults.condition);
    fill(s.unit, defaults.unit);
    fill(s.service, defaults.service);
    fill(s.install, defaults.install);
    for (const auto& kv : defaults.vars) s.vars.emplace(kv.first, kv.second);
    return s;
}

// systemd runs ExecStart without a shell, so a command that needs one is handed to bash
inline std::string exec_start(const std::string& command)
{
    static const std::vector<std::string> shell_syntax{"<(", "$(", "|", "&&", ";", "`", ">"};
    bool needs_shell =
        std::any_of(shell_syntax.begin(), shell_syntax.end(),
                    [&](const std::string& s) { return command.find(s) != std::string::npos; });
    if (!needs_shell)
        return command;
    // '$' is doubled so that systemd passes it on to bash
    auto quoted = replace_all(replace_all(command, "'", "'\\''"), "$", "$$");
    return "/bin/bash -c '" + quoted + "'";
}

// replaces ${name} for each var; anything else (systemd's own $MAINPID, %i) is left alone
inline std::string substitute(std::string text, const std::map<std::string, std::string>& vars)
{
    for (const auto& kv : vars) text = replace_all(text, "${" + kv.first + "}", kv.second);
    return text;
}
} // namespace detail

/// \brief The target (generated, or the platform's systemd: target:) that a unit's services belong to
inline std::string systemd_target(const Deployment& deployment, const LaunchUnit& unit)
{
    if (unit.platform->systemd.target)
        return *unit.platform->systemd.target;
    return detail::unit_name(deployment.name + "-" + unit.name()) + ".target";
}

/// \brief The systemd units that start a launch unit: one service per enabled entry and, unless the
/// platform names its own, a target that brings them all up
inline std::vector<SystemdUnitFile> render_systemd(const Deployment& deployment,
                                                   const LaunchUnit& unit, Issues& issues)
{
    const auto& platform = *unit.platform;
    auto prefix = unit.module.empty() ? "" : detail::unit_name(unit.module) + "-";
    auto service_name = [&](const std::string& name)
    { return prefix + detail::unit_name(name) + ".service"; };
    auto target = systemd_target(deployment, unit);
    auto instance = platform.name.substr(std::min(platform.base_name.size(), platform.name.size()));

    auto lines = launch_lines(unit);
    std::string gobyd_service;
    for (const auto& line : lines)
    {
        if (line.entry->enabled && detail::is_gobyd(*line.entry))
            gobyd_service = service_name(line.name);
    }

    std::vector<SystemdUnitFile> units;
    std::vector<std::string> services;
    // entries that goby_launch would start but that get no unit here
    std::vector<std::string> dropped;
    std::string previous;
    for (const auto& line : lines)
    {
        const auto& e = *line.entry;
        auto settings = detail::merged(platform.systemd.defaults, e.systemd);
        if (!e.enabled || settings.skip)
            continue;
        if (e.when)
        {
            auto decided = detail::evaluate_when(*e.when);
            if (!decided)
            {
                add_issue(issues, Issue::Severity::WARNING, "systemd-when",
                          e.name + ": when: '" + *e.when +
                              "' cannot be decided before launch, so no unit is written; set its "
                              "values with -D, or give systemd: { condition: ... }",
                          e.location);
            }
            if (!decided || !*decided)
            {
                dropped.push_back(line.name);
                continue;
            }
        }

        auto service = service_name(line.name);
        auto kill =
            line.kill.empty() ? (line.goby_application ? "goby_terminate" : "SIGTERM") : line.kill;
        bool needs_gobyd = line.goby_application && !detail::is_gobyd(e) && !gobyd_service.empty();
        auto description = settings.description.value_or(line.name + " (" + unit.name() + " of " +
                                                         deployment.name + ")");

        std::string environment;
        for (const auto& kv : line.env)
            environment += "Environment=\"" + kv.first + "=" + kv.second + "\"\n";

        std::string text;
        if (settings.unit_template)
        {
            auto path = std::filesystem::path(*settings.unit_template);
            if (path.is_relative())
                path = std::filesystem::path(deployment.file).parent_path() / path;
            std::ifstream in(path);
            if (!in.is_open())
            {
                add_issue(issues, Issue::Severity::ERROR, "systemd-template",
                          e.name + ": cannot open template " + path.string(), e.location);
                continue;
            }
            std::stringstream ss;
            ss << in.rdbuf();

            auto config = line.goby_application && !e.config
                              ? platform.launch_settings.config.value_or("")
                              : e.config.value_or("");
            auto args = line.goby_application && !e.args
                            ? platform.launch_settings.args.value_or("")
                            : e.args.value_or("");
            std::map<std::string, std::string> vars{
                {"name", line.name},
                {"unit_name", service},
                {"application", e.application},
                {"platform", platform.name},
                {"instance", instance},
                {"deployment", deployment.name},
                {"description", description},
                {"exec", e.exec.value_or(e.process ? e.name : e.application)},
                {"command", line.unconditional_command},
                {"exec_start", detail::exec_start(line.unconditional_command)},
                {"config", detail::replace_all(config, "${app}", line.name)},
                {"args", detail::replace_all(args, "${app}", line.name)},
                {"env", environment},
                {"kill", kill},
                {"user", settings.user.value_or("")},
                {"group", settings.group.value_or("")},
                {"wanted_by", settings.wanted_by.value_or(target)},
                {"target", target},
                {"unit", settings.unit.value_or("")},
                {"service", settings.service.value_or("")},
                {"install", settings.install.value_or("")}};
            for (const auto& kv : settings.vars) vars[kv.first] = kv.second;
            text = detail::substitute(ss.str(), vars);
        }
        else
        {
            std::stringstream ss;
            ss << "# Generated by 'goby interface systemd' from " << deployment.file
               << ": edit that file rather than this one.\n";
            ss << "[Unit]\n";
            ss << "Description=" << description << "\n";
            ss << "PartOf=" << target << "\n";
            if (needs_gobyd)
                ss << "After=" << gobyd_service << "\nBindsTo=" << gobyd_service << "\n";
            if (platform.systemd.ordered && !previous.empty() &&
                !(needs_gobyd && previous == gobyd_service))
                ss << "After=" << previous << "\n";
            if (settings.condition)
                ss << *settings.condition << "\n";
            if (settings.unit)
                ss << *settings.unit << "\n";

            ss << "\n[Service]\n";
            if (settings.user)
                ss << "User=" << *settings.user << "\n";
            if (settings.group)
                ss << "Group=" << *settings.group << "\n";
            ss << environment;
            ss << "ExecStart=" << detail::exec_start(line.unconditional_command) << "\n";
            if (line.goby_application && kill == "goby_terminate")
                ss << "ExecStop=goby_terminate --target_pid $MAINPID\n";
            else
                ss << "KillSignal=" << (kill == "goby_terminate" ? "SIGTERM" : kill) << "\n";
            ss << "Restart=on-failure\n";
            if (settings.service)
                ss << *settings.service << "\n";

            ss << "\n[Install]\n";
            ss << "WantedBy=" << settings.wanted_by.value_or(target) << "\n";
            if (settings.install)
                ss << *settings.install << "\n";
            text = ss.str();
        }

        if (text.find("${") != std::string::npos)
            add_issue(issues, Issue::Severity::INFO, "systemd-unexpanded",
                      service + " still has a ${...} that systemd will read as its own variable",
                      e.location);
        units.push_back({service, text});
        services.push_back(service);
        previous = service;
    }

    // gobyd would hold, or goby_coroner report dead, an application that is never started
    for (const auto& line : lines)
    {
        const auto& e = *line.entry;
        std::vector<std::string> waits_for;
        if (e.hold)
            waits_for = detail::expand_auto(*e.hold, auto_hold(unit));
        if (e.expected)
        {
            auto expected = detail::expand_auto(*e.expected, auto_expected(unit, e));
            waits_for.insert(waits_for.end(), expected.begin(), expected.end());
        }
        for (const auto& name : dropped)
        {
            if (std::find(waits_for.begin(), waits_for.end(), name) != waits_for.end())
                add_issue(issues, Issue::Severity::WARNING, "systemd-dropped",
                          line.name + " waits for " + name + ", which has no unit", e.location);
        }
    }

    if (!platform.systemd.target)
    {
        std::stringstream ss;
        ss << "# Generated by 'goby interface systemd' from " << deployment.file
           << ": edit that file rather than this one.\n";
        ss << "[Unit]\n";
        ss << "Description=" << unit.name() << " of " << deployment.name << "\n";
        if (!services.empty())
        {
            ss << "Wants=";
            for (std::size_t i = 0; i < services.size(); ++i) ss << (i ? " " : "") << services[i];
            ss << "\n";
        }
        ss << "\n[Install]\nWantedBy=multi-user.target\n";
        units.insert(units.begin(), {target, ss.str()});
    }
    return units;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
