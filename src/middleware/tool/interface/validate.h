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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_VALIDATE_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_VALIDATE_H

#include <map>
#include <set>
#include <string>
#include <tuple>

#include "goby/middleware/tool/interface/resolve.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
struct ValidateParams
{
    // by default everything is checked, including the groups a diagram hides
    ViewFilter::Params filter{"", "", true, true, true, false};
    // report publications that nothing subscribes to (common, and usually fine, for libraries)
    bool report_unused{false};
    // count a dynamic publisher whose numeric is set at run time as reaching its subscribers
    bool assume_dynamic{false};
};

struct ValidationSummary
{
    int platforms{0};
    int applications{0};
    int connections{0};
    int required{0};
    int required_met{0};
};

namespace detail
{
inline Issue::Subject subject(const Unmatched& u)
{
    Issue::Subject s;
    s.platform = u.at.platform->name;
    if (u.at.platform->has_modules())
        s.module = u.at.module->name;
    s.application = u.at.application->name;
    s.thread = u.entry.thread;
    s.layer = layer_name(u.entry.layer);
    s.group = u.entry.display_group();
    s.scheme = u.entry.scheme;
    s.type = u.entry.type;
    if (u.entry.direction == PubSubEntry::Direction::SUBSCRIBE)
        s.necessity = necessity_name(u.entry.necessity);
    return s;
}

inline std::string describe(const PubSubEntry& e)
{
    return e.display_group() + " (" + e.scheme + " " + e.type + ")";
}
} // namespace detail

/// \brief Check every required and recommended subscription in a resolved deployment
///
/// Adds an issue for each problem and returns counts for a summary line.
inline ValidationSummary validate(const Deployment& deployment, const Resolution& resolution,
                                  const ValidateParams& params, Issues& issues)
{
    ViewFilter filter(params.filter);
    ValidationSummary summary;

    for (const auto& platform : deployment.platforms)
    {
        ++summary.platforms;
        for (const auto& module : platform.modules)
            summary.applications += module.applications.size();
    }

    // subscription -> reached by a publisher whose group certainly matches
    std::map<std::tuple<const Application*, PubSubEntry>, bool> reached;
    for (const auto& c : resolution.connections)
    {
        if (!filter.is_included(c))
            continue;
        ++summary.connections;
        auto it = reached.emplace(std::make_tuple(c.sub_at.application, c.sub), false).first;
        it->second = it->second || !c.loose;
    }

    auto add = [&](Issue::Severity severity, const std::string& code, const std::string& message,
                   const Unmatched& u)
    {
        Issue issue;
        issue.severity = severity;
        issue.code = code;
        issue.message = message;
        issue.subject = detail::subject(u);
        issues.push_back(issue);
    };

    std::vector<Unmatched> pubs, subs;
    collect_entries(deployment, pubs, subs);

    for (const auto& u : subs)
    {
        if (!filter.is_included(u) || u.entry.necessity == goby::middleware::Necessity::OPTIONAL)
            continue;

        bool required = u.entry.necessity == goby::middleware::Necessity::REQUIRED;
        auto it = reached.find(std::make_tuple(u.at.application, u.entry));
        bool met = it != reached.end() && (it->second || params.assume_dynamic);
        if (required)
            ++summary.required;
        if (met)
        {
            if (required)
                ++summary.required_met;
            continue;
        }

        std::string message =
            it == reached.end()
                ? "no publisher for " + detail::describe(u.entry)
                : "only a dynamic publisher with a numeric set at run time reaches " +
                      detail::describe(u.entry) + " (give its numeric with bind:, or use " +
                      "--assume_dynamic)";
        if (u.entry.necessity_overridden)
            message += " (necessity set by the deployment)";
        if (required)
            add(Issue::Severity::ERROR, "required-unmet", message, u);
        else
            add(Issue::Severity::WARNING, "recommended-unmet", message, u);
    }

    for (const auto* entries : {&pubs, &subs})
    {
        for (const auto& u : *entries)
        {
            if (!filter.is_included(u))
                continue;
            if (u.entry.unresolved())
                add(Issue::Severity::INFO, "dynamic-unresolved",
                    "goby_clang_tool could not determine this dynamic group" +
                        (u.entry.expr.empty() ? std::string() : " (" + u.entry.expr + ")") +
                        "; a dynamic: bind can say what it stands for",
                    u);
            else if (u.entry.unbound())
                add(Issue::Severity::WARNING, "dynamic-unbound",
                    "the numeric part of " + u.entry.group +
                        " is set at run time; give it with bind: { group: " + u.entry.group +
                        ", numeric: ... }",
                    u);
        }
    }

    if (params.report_unused)
    {
        for (const auto& u : resolution.unsubscribed)
        {
            if (u.entry.is_inner_pub || !filter.is_included(u))
                continue;
            add(Issue::Severity::INFO, "publisher-unused",
                "no subscriber for " + detail::describe(u.entry), u);
        }
    }

    return summary;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
