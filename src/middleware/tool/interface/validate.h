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
    s.group = u.entry.group;
    s.scheme = u.entry.scheme;
    s.type = u.entry.type;
    if (u.entry.direction == PubSubEntry::Direction::SUBSCRIBE)
        s.necessity = necessity_name(u.entry.necessity);
    return s;
}

inline std::string describe(const PubSubEntry& e)
{
    return e.group + " (" + e.scheme + " " + e.type + ")";
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

    for (const auto& c : resolution.connections)
    {
        if (!filter.is_included(c))
            continue;
        ++summary.connections;
    }

    // a subscription is required once, however many publishers reach it
    std::set<std::tuple<const Application*, PubSubEntry>> required_met;
    for (const auto& c : resolution.connections)
    {
        if (filter.is_included(c) && c.sub.necessity == goby::middleware::Necessity::REQUIRED)
            required_met.emplace(c.sub_at.application, c.sub);
    }
    summary.required_met = required_met.size();
    summary.required = summary.required_met;

    for (const auto& u : resolution.unpublished)
    {
        if (!filter.is_included(u))
            continue;

        Issue issue;
        switch (u.entry.necessity)
        {
            case goby::middleware::Necessity::REQUIRED:
                ++summary.required;
                issue.severity = Issue::Severity::ERROR;
                issue.code = "required-unmet";
                break;
            case goby::middleware::Necessity::RECOMMENDED:
                issue.severity = Issue::Severity::WARNING;
                issue.code = "recommended-unmet";
                break;
            case goby::middleware::Necessity::OPTIONAL: continue;
        }
        issue.message = "no publisher for " + detail::describe(u.entry);
        issue.subject = detail::subject(u);
        issues.push_back(issue);
    }

    if (params.report_unused)
    {
        for (const auto& u : resolution.unsubscribed)
        {
            if (u.entry.is_inner_pub || !filter.is_included(u))
                continue;
            Issue issue;
            issue.severity = Issue::Severity::INFO;
            issue.code = "publisher-unused";
            issue.message = "no subscriber for " + detail::describe(u.entry);
            issue.subject = detail::subject(u);
            issues.push_back(issue);
        }
    }

    return summary;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
