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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_LAUNCH_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_LAUNCH_H

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "goby/middleware/tool/interface/model.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
/// \brief What one launch file starts: a platform, or one module (one gobyd) of a platform
struct LaunchUnit
{
    const Platform* platform{nullptr};
    // "" for a platform without modules
    std::string module;

    std::string name() const { return platform->name + (module.empty() ? "" : "." + module); }
};

/// \brief One goby_launch line, worked out from a run: entry
struct LaunchLine
{
    const LaunchEntry* entry{nullptr};
    std::string name;
    std::string command;
    // the command without its when: condition
    std::string unconditional_command;
    // as given in the deployment (empty: goby_launch's default)
    std::string kill;
    KeyValues env;
    bool goby_application{false};
};

namespace detail
{
inline std::string replace_all(std::string text, const std::string& from, const std::string& to)
{
    for (auto pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos))
    {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

inline std::string first_word_basename(const std::string& command)
{
    auto word = command.substr(0, command.find(' '));
    auto slash = word.rfind('/');
    return slash == std::string::npos ? word : word.substr(slash + 1);
}

inline bool in_unit(const LaunchEntry& e, const LaunchUnit& unit)
{
    return unit.module.empty() || e.module == unit.module;
}

inline const Application* application(const LaunchUnit& unit, const LaunchEntry& e)
{
    for (const auto& module : unit.platform->modules)
    {
        if (!unit.module.empty() && module.name != unit.module)
            continue;
        auto it = module.applications.find(e.name);
        if (it != module.applications.end())
            return &it->second;
    }
    return nullptr;
}

inline bool requires_intervehicle(const Application& app)
{
    for (const auto& e : app.subscribes_on(Layer::INTERVEHICLE))
    {
        if (e.necessity == goby::middleware::Necessity::REQUIRED)
            return true;
    }
    return false;
}

/// \brief A when: condition as a shell command: "A =~ REGEX", "A == B", "A != B", or a command
/// whose exit status decides. Kept free of '[' and ']', which end goby_launch's [directives].
inline std::string when_command(const std::string& when)
{
    for (const std::string op : {" =~ ", " == ", " != "})
    {
        auto pos = when.find(op);
        if (pos == std::string::npos)
            continue;
        auto lhs = when.substr(0, pos), rhs = when.substr(pos + op.size());
        if (op == " =~ ")
            return "echo \"" + lhs + "\" | grep -qE \"" + rhs + "\"";
        return "test \"" + lhs + "\" " + (op == " == " ? "=" : "!=") + " \"" + rhs + "\"";
    }
    return when;
}

inline std::string escape_double_quoted(const std::string& text)
{
    std::string escaped;
    for (char c : text)
    {
        if (c == '"' || c == '\\')
            escaped += '\\';
        escaped += c;
    }
    return escaped;
}

inline bool is_gobyd(const LaunchEntry& e)
{
    return !e.process && (e.application == "gobyd" || e.name == "gobyd");
}
} // namespace detail

/// \brief Every launch file a deployment makes: one per platform, or per module where a platform
/// has modules
inline std::vector<LaunchUnit> launch_units(const Deployment& deployment)
{
    std::vector<LaunchUnit> units;
    for (const auto& platform : deployment.platforms)
    {
        if (!platform.has_modules())
        {
            units.push_back({&platform, ""});
            continue;
        }
        for (const auto& module : platform.modules) units.push_back({&platform, module.name});
    }
    return units;
}

/// \brief gobyd's hold: applications with a required intervehicle subscription, which must be
/// subscribed before anything is published to them, and goby_intervehicle_portal, which carries
/// their subscriptions
inline std::vector<std::string> auto_hold(const LaunchUnit& unit)
{
    std::vector<std::string> clients;
    for (const auto& e : unit.platform->launch)
    {
        if (!e.enabled || e.process || !detail::in_unit(e, unit))
            continue;
        const auto* app = detail::application(unit, e);
        if ((app && detail::requires_intervehicle(*app)) ||
            e.application == "goby_intervehicle_portal")
            clients.push_back(e.name);
    }
    return clients;
}

/// \brief goby_coroner's expected names: every Goby application in the unit but the coroner itself
/// and those marked expected: false
inline std::vector<std::string> auto_expected(const LaunchUnit& unit, const LaunchEntry& coroner)
{
    std::vector<std::string> names;
    for (const auto& e : unit.platform->launch)
    {
        if (!e.enabled || e.process || e.not_expected || &e == &coroner ||
            !detail::in_unit(e, unit))
            continue;
        names.push_back(e.name);
    }
    return names;
}

namespace detail
{
// a list with "auto" in it, with the derived list in its place
inline std::vector<std::string> expand_auto(const std::vector<std::string>& given,
                                            const std::vector<std::string>& derived)
{
    std::vector<std::string> out;
    for (const auto& item : given)
    {
        const auto& add = item == "auto" ? derived : std::vector<std::string>{item};
        for (const auto& name : add)
        {
            if (std::find(out.begin(), out.end(), name) == out.end())
                out.push_back(name);
        }
    }
    return out;
}
} // namespace detail

/// \brief The goby_launch lines for a unit, in run: order (disabled entries included)
inline std::vector<LaunchLine> launch_lines(const LaunchUnit& unit)
{
    const auto& settings = unit.platform->launch_settings;
    std::vector<LaunchLine> lines;
    for (const auto& e : unit.platform->launch)
    {
        if (!detail::in_unit(e, unit))
            continue;
        LaunchLine line;
        line.entry = &e;
        line.name = e.name;
        line.goby_application = !e.process;

        auto pick = [&](const std::optional<std::string>& own,
                        const std::optional<std::string>& fallback) -> std::string
        {
            if (own)
                return *own;
            return line.goby_application && fallback ? *fallback : "";
        };
        auto app = [&](const std::string& text)
        { return detail::replace_all(text, "${app}", e.name); };

        std::string command = app(e.exec ? *e.exec : (e.process ? e.name : e.application));
        for (const auto& part : {pick(e.config, settings.config), pick(e.args, settings.args)})
        {
            if (!part.empty())
                command += " " + app(part);
        }

        if (e.hold)
        {
            std::string clients;
            for (const auto& client : detail::expand_auto(*e.hold, auto_hold(unit)))
                clients += (clients.empty() ? "" : " ") + std::string("required_client: \"") +
                           client + "\"";
            if (!clients.empty())
                command += " --hold '" + clients + "'";
        }
        if (e.expected)
        {
            for (const auto& name : detail::expand_auto(*e.expected, auto_expected(unit, e)))
                command += " --expected_name " + name;
        }
        line.unconditional_command = command;
        if (e.when)
            command = "/bin/bash -c \"if " +
                      detail::escape_double_quoted(detail::when_command(*e.when)) + "; then exec " +
                      detail::escape_double_quoted(command) + "; else exec sleep infinity; fi\"";
        line.command = command;

        line.kill = pick(e.kill, settings.kill);
        if (line.kill.empty() && e.process)
            line.kill = "SIGTERM";

        if (line.goby_application)
            line.env = settings.env;
        for (const auto& kv : e.env)
        {
            auto it = std::find_if(line.env.begin(), line.env.end(),
                                   [&](const std::pair<std::string, std::string>& existing)
                                   { return existing.first == kv.first; });
            if (it == line.env.end())
                line.env.push_back(kv);
            else
                it->second = kv.second;
        }
        for (auto& kv : line.env) kv.second = app(kv.second);
        lines.push_back(line);
    }
    return lines;
}

/// \brief "[kill=..., name=..., env=...] command", as goby_launch reads it
inline std::string launch_line_text(const LaunchLine& line)
{
    std::vector<std::string> directives;
    if (!line.kill.empty() && !(line.goby_application && line.kill == "goby_terminate"))
        directives.push_back("kill=" + line.kill);
    if (detail::first_word_basename(line.command) != line.name)
        directives.push_back("name=" + line.name);
    for (const auto& kv : line.env) directives.push_back("env=" + kv.first + "=" + kv.second);

    std::string text;
    if (!directives.empty())
    {
        text = "[";
        for (std::size_t i = 0; i < directives.size(); ++i) text += (i ? ", " : "") + directives[i];
        text += "] ";
    }
    return text + line.command;
}

/// \brief A goby_launch file for the unit
inline std::string render_launch(const LaunchUnit& unit, const std::string& deployment_file)
{
    std::stringstream ss;
    auto flags = unit.platform->launch_settings.flags;
    if (flags.find("-p ") == std::string::npos && flags.find("-p\t") == std::string::npos)
        flags += (flags.empty() ? "" : " ") + std::string("-p ") + unit.name();
    ss << "#!/usr/bin/env -S goby_launch " << flags << "\n";
    ss << "# Generated by 'goby interface launch' from " << deployment_file << " for "
       << unit.name() << ": edit that file rather than this one.\n";
    for (const auto& line : launch_lines(unit))
        ss << (line.entry->enabled ? "" : "# (enabled: false) ") << launch_line_text(line) << "\n";
    return ss.str();
}

/// \brief Problems with a deployment's launch data: anything goby_launch would cut at a '#', a Goby
/// application listed before gobyd, a hold list that misses a required intervehicle subscriber,
/// and an expected list that differs from what is launched
inline void check_launch(const Deployment& deployment, Issues& issues)
{
    for (const auto& unit : launch_units(deployment))
    {
        bool seen_gobyd = std::none_of(
            unit.platform->launch.begin(), unit.platform->launch.end(), [&](const LaunchEntry& e)
            { return e.enabled && detail::in_unit(e, unit) && detail::is_gobyd(e); });
        for (const auto& line : launch_lines(unit))
        {
            const auto& e = *line.entry;
            if (!e.enabled)
                continue;
            auto text = launch_line_text(line);
            if (text.find('#') != std::string::npos)
                add_issue(issues, Issue::Severity::ERROR, "launch-hash",
                          e.name + ": goby_launch drops everything after a '#' on a line",
                          e.location);
            // goby_launch reads [directives] up to the last ']' on the line
            if (text[0] == '[' && line.command.find(']') != std::string::npos)
                add_issue(issues, Issue::Severity::ERROR, "launch-bracket",
                          e.name + ": goby_launch would read the command's ']' as the end of the "
                                   "line's [directives]",
                          e.location);
            if (detail::is_gobyd(e))
                seen_gobyd = true;
            else if (!e.process && !seen_gobyd)
                add_issue(issues, Issue::Severity::WARNING, "launch-order",
                          e.name + " is launched before gobyd on " + unit.name(), e.location);

            if (e.hold && std::find(e.hold->begin(), e.hold->end(), "auto") == e.hold->end())
            {
                for (const auto& client : auto_hold(unit))
                {
                    if (std::find(e.hold->begin(), e.hold->end(), client) == e.hold->end())
                        add_issue(issues, Issue::Severity::WARNING, "hold-incomplete",
                                  "the hold leaves out " + client +
                                      ", which needs its intervehicle subscriptions in place "
                                      "first (hold: auto adds it)",
                                  e.location);
                }
            }
            if (e.expected &&
                std::find(e.expected->begin(), e.expected->end(), "auto") == e.expected->end())
            {
                auto launched = auto_expected(unit, e);
                std::set<std::string> given(e.expected->begin(), e.expected->end()),
                    actual(launched.begin(), launched.end());
                std::string missing, extra;
                for (const auto& name : actual)
                    if (!given.count(name))
                        missing += (missing.empty() ? "" : ", ") + name;
                for (const auto& name : given)
                    if (!actual.count(name))
                        extra += (extra.empty() ? "" : ", ") + name;
                if (!missing.empty() || !extra.empty())
                    add_issue(issues, Issue::Severity::INFO, "coroner-expected",
                              "the expected list " +
                                  (missing.empty() ? "" : "leaves out " + missing) +
                                  (!missing.empty() && !extra.empty() ? " and " : "") +
                                  (extra.empty() ? "" : "names " + extra + ", not launched here"),
                              e.location);
            }
        }
    }
}

/// \brief One line of an existing launch file, as goby_launch would read it
struct ParsedLaunchLine
{
    int line_number{0};
    std::string name;
    std::string command;
    std::string kill;
    KeyValues env;
};

inline std::vector<ParsedLaunchLine> parse_launch_file(const std::string& text)
{
    std::vector<ParsedLaunchLine> lines;
    std::stringstream in(text);
    int number = 0;
    for (std::string raw; std::getline(in, raw);)
    {
        ++number;
        auto line = raw.substr(0, raw.find('#'));
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);

        ParsedLaunchLine parsed;
        parsed.line_number = number;
        if (line[0] == '[')
        {
            auto close = line.find(']');
            std::string directives = line.substr(1, close - 1);
            line = close == std::string::npos ? "" : line.substr(close + 1);
            line = line.substr(std::min(line.size(), line.find_first_not_of(" \t")));
            for (auto& c : directives)
            {
                if (c == ',' || c == ';')
                    c = ' ';
            }
            std::stringstream ds(directives);
            for (std::string directive; ds >> directive;)
            {
                auto eq = directive.find('=');
                auto key = directive.substr(0, eq);
                auto value = eq == std::string::npos ? "" : directive.substr(eq + 1);
                if (key == "kill")
                    parsed.kill = value;
                else if (key == "name")
                    parsed.name = value;
                else if (key == "env")
                {
                    auto env_eq = value.find('=');
                    parsed.env.emplace_back(value.substr(0, env_eq),
                                            env_eq == std::string::npos ? ""
                                                                        : value.substr(env_eq + 1));
                }
            }
        }
        parsed.command = line;
        if (parsed.name.empty())
            parsed.name = detail::first_word_basename(line);
        lines.push_back(parsed);
    }
    return lines;
}

/// \brief A difference between a launch file and a deployment's launch lines
struct LaunchDifference
{
    // launched-only, not-launched, env, kill
    std::string kind;
    std::string name;
    int line_number{0};
    std::string detail;
};

/// \brief Compare a hand-written launch file with what the deployment would launch
inline std::vector<LaunchDifference> check_launch_file(const LaunchUnit& unit,
                                                       const std::string& launch_text)
{
    auto parsed = parse_launch_file(launch_text);
    std::vector<LaunchLine> expected;
    for (const auto& line : launch_lines(unit))
    {
        if (line.entry->enabled)
            expected.push_back(line);
    }

    // goby_launch kills with goby_terminate only when it finds exactly one gobyd to ask
    auto default_kill = [](int gobyd_count)
    { return gobyd_count == 1 ? "goby_terminate" : "SIGTERM"; };
    int file_gobyds = std::count_if(parsed.begin(), parsed.end(), [](const ParsedLaunchLine& l)
                                    { return detail::first_word_basename(l.command) == "gobyd"; });
    int deployment_gobyds = std::count_if(expected.begin(), expected.end(), [](const LaunchLine& l)
                                          { return detail::is_gobyd(*l.entry); });

    auto env_text = [](KeyValues env)
    {
        std::sort(env.begin(), env.end());
        std::string text;
        for (const auto& kv : env) text += (text.empty() ? "" : " ") + kv.first + "=" + kv.second;
        return text.empty() ? "(none)" : text;
    };

    std::vector<LaunchDifference> differences;
    for (const auto& p : parsed)
    {
        auto it = std::find_if(expected.begin(), expected.end(),
                               [&](const LaunchLine& l) { return l.name == p.name; });
        if (it == expected.end())
        {
            differences.push_back(
                {"launched-only", p.name, p.line_number,
                 "line " + std::to_string(p.line_number) + " has no entry in the deployment"});
            continue;
        }
        auto file_kill = p.kill.empty() ? default_kill(file_gobyds) : p.kill;
        auto deployment_kill = it->kill.empty() ? default_kill(deployment_gobyds) : it->kill;
        if (file_kill != deployment_kill)
            differences.push_back(
                {"kill", p.name, p.line_number,
                 file_kill + " in the launch file, " + deployment_kill + " in the deployment"});
        if (env_text(p.env) != env_text(it->env))
            differences.push_back({"env", p.name, p.line_number,
                                   env_text(p.env) + " in the launch file, " + env_text(it->env) +
                                       " in the deployment"});
    }
    for (const auto& l : expected)
    {
        if (std::none_of(parsed.begin(), parsed.end(),
                         [&](const ParsedLaunchLine& p) { return p.name == l.name; }))
            differences.push_back(
                {"not-launched", l.name, 0,
                 "in the deployment (" + l.entry->location + ") but not in the launch file"});
    }
    return differences;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
