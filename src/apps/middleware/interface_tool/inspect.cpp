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

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

#include "goby/middleware/tool/interface/validate.h"

#include "common.h"
#include "inspect.h"

using goby::middleware::tool::interface::GroupMatch;
using goby::middleware::tool::interface::Layer;
using goby::middleware::tool::interface::PubSubEntry;
using goby::middleware::tool::interface::Unmatched;

namespace goby
{
namespace apps
{
namespace middleware
{
namespace interface_tool
{
namespace
{
const std::vector<Layer> layers{Layer::INTERTHREAD, Layer::INTERPROCESS, Layer::INTERMODULE,
                                Layer::INTERVEHICLE};

protobuf::InterfaceEndpoint endpoint(const Unmatched& u)
{
    protobuf::InterfaceEndpoint e;
    to_proto(interface::detail::subject(u), e);
    return e;
}

protobuf::InterfaceEndpoint endpoint(const PubSubEntry& entry, const interface::Endpoint& at)
{
    return endpoint(Unmatched{entry, at});
}

// the unqualified class name, without template arguments
std::string short_thread(const std::string& thread)
{
    auto name = thread.substr(0, thread.find('<'));
    auto scope = name.rfind("::");
    return scope == std::string::npos ? name : name.substr(scope + 2);
}

// "auv/controller (Controller)"
std::string where(const protobuf::InterfaceEndpoint& e)
{
    return application_path(e) + (e.thread().empty() ? "" : " (" + short_thread(e.thread()) + ")");
}

std::string what(const protobuf::InterfaceEndpoint& e)
{
    return e.group() + " " + e.scheme() + " " + e.type();
}

bool same(const PubSubEntry& a, const PubSubEntry& b) { return !(a < b) && !(b < a); }

std::string plural(int n, const std::string& word)
{
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

std::string application_key(const interface::Platform& platform, const interface::Module& module,
                            const interface::Application& app)
{
    return platform.name + "/" + (platform.has_modules() ? module.name + "/" : "") + app.name;
}
} // namespace

protobuf::InterfaceList list(const std::string& file, const interface::Deployment& deployment)
{
    protobuf::InterfaceList out;
    out.set_file(file);

    for (const auto& platform : deployment.platforms)
    {
        auto& p = *out.add_platform();
        p.set_name(platform.name);
        int applications = 0;
        for (const auto& module : platform.modules)
        {
            if (platform.has_modules())
                p.add_module(module.name);
            for (const auto& app_p : module.applications)
            {
                const auto& app = app_p.second;
                ++applications;
                auto& a = *out.add_application();
                a.set_platform(platform.name);
                if (platform.has_modules())
                    a.set_module(module.name);
                a.set_name(app.name);

                std::set<std::string> threads;
                std::map<Layer, std::pair<int, int>> counts;
                for (const auto& thread_p : app.threads)
                {
                    threads.insert(thread_p.second->most_derived_name());
                    for (const auto& e : thread_p.second->interthread_publishes)
                        counts[Layer::INTERTHREAD].first += !e.is_inner_pub;
                    counts[Layer::INTERTHREAD].second +=
                        thread_p.second->interthread_subscribes.size();
                }
                for (auto layer : {Layer::INTERPROCESS, Layer::INTERMODULE, Layer::INTERVEHICLE})
                {
                    for (const auto& e : app.publishes_on(layer))
                        counts[layer].first += !e.is_inner_pub;
                    counts[layer].second += app.subscribes_on(layer).size();
                }
                for (const auto& thread : threads) a.add_thread(thread);
                for (auto layer : layers)
                {
                    const auto& count = counts[layer];
                    if (count.first == 0 && count.second == 0)
                        continue;
                    auto& c = *a.add_count();
                    c.set_layer(interface::layer_name(layer));
                    c.set_publishes(count.first);
                    c.set_subscribes(count.second);
                }
            }
        }
        p.set_applications(applications);
    }

    std::vector<Unmatched> pubs, subs;
    interface::collect_entries(deployment, pubs, subs);
    std::map<std::pair<Layer, std::string>, protobuf::InterfaceList::Group> groups;
    auto group_for = [&](const PubSubEntry& e) -> protobuf::InterfaceList::Group&
    {
        auto& g = groups[{e.layer, e.display_group()}];
        g.set_layer(interface::layer_name(e.layer));
        g.set_group(e.display_group());
        return g;
    };
    for (const auto& u : pubs)
    {
        if (!u.entry.is_inner_pub)
            *group_for(u.entry).add_publisher() = endpoint(u);
    }
    for (const auto& u : subs) *group_for(u.entry).add_subscriber() = endpoint(u);
    for (auto& g : groups) *out.add_group() = g.second;

    return out;
}

std::string render_human(const protobuf::InterfaceList& list,
                         protobuf::InterfaceListConfig::What what_to_list)
{
    std::stringstream ss;
    // the columns are padded, so trailing spaces are trimmed after
    auto trimmed = [&ss]()
    {
        std::stringstream in(ss.str()), out;
        for (std::string line; std::getline(in, line);)
            out << line.substr(0, line.find_last_not_of(' ') + 1) << "\n";
        return out.str();
    };
    switch (what_to_list)
    {
        case protobuf::InterfaceListConfig::platforms:
        {
            std::size_t width = 8;
            for (const auto& p : list.platform()) width = std::max(width, p.name().size());
            ss << std::left << std::setw(width + 2) << "PLATFORM" << std::setw(14) << "APPLICATIONS"
               << "MODULES\n";
            for (const auto& p : list.platform())
            {
                std::string modules;
                for (const auto& m : p.module()) modules += (modules.empty() ? "" : ", ") + m;
                ss << std::setw(width + 2) << p.name() << std::setw(14) << p.applications()
                   << (modules.empty() ? "-" : modules) << "\n";
            }
            break;
        }

        case protobuf::InterfaceListConfig::applications:
        {
            std::size_t width = 11;
            for (const auto& a : list.application())
            {
                protobuf::InterfaceEndpoint e;
                e.set_platform(a.platform());
                if (a.has_module())
                    e.set_module(a.module());
                e.set_application(a.name());
                width = std::max(width, application_path(e).size());
            }
            ss << std::left << std::setw(width + 2) << "APPLICATION" << std::setw(9) << "THREADS";
            for (auto layer : layers) ss << std::setw(14) << interface::layer_name(layer);
            ss << "(publications/subscriptions)\n";
            for (const auto& a : list.application())
            {
                protobuf::InterfaceEndpoint e;
                e.set_platform(a.platform());
                if (a.has_module())
                    e.set_module(a.module());
                e.set_application(a.name());
                ss << std::setw(width + 2) << application_path(e) << std::setw(9)
                   << a.thread_size();
                for (auto layer : layers)
                {
                    std::string cell = "-";
                    for (const auto& c : a.count())
                    {
                        if (c.layer() == interface::layer_name(layer))
                            cell = std::to_string(c.publishes()) + "/" +
                                   std::to_string(c.subscribes());
                    }
                    ss << std::setw(14) << cell;
                }
                ss << "\n";
            }
            break;
        }

        case protobuf::InterfaceListConfig::groups:
        {
            for (const auto& g : list.group())
            {
                ss << g.layer() << "  " << g.group() << "\n";
                for (const auto& p : g.publisher())
                    ss << "  publisher   " << where(p) << "  " << p.scheme() << " " << p.type()
                       << "\n";
                for (const auto& s : g.subscriber())
                    ss << "  subscriber  " << where(s) << "  " << s.scheme() << " " << s.type()
                       << (s.necessity() == "optional" ? "" : "  " + s.necessity()) << "\n";
            }
            break;
        }
    }
    return trimmed();
}

namespace
{
std::string scope_reason(const std::string& who, const interface::Unmatched& pub)
{
    switch (pub.entry.layer)
    {
        case Layer::INTERTHREAD:
            return who + " publishes it on interthread, which stays within its application";
        case Layer::INTERPROCESS:
            return who + " publishes it on interprocess in module " + pub.at.module->name +
                   ", but interprocess stops at its module (one gobyd); it needs intermodule";
        case Layer::INTERMODULE:
            return who + " publishes it on intermodule on platform " + pub.at.platform->name +
                   ", but intermodule stays on its platform; it needs intervehicle";
        case Layer::INTERVEHICLE:
            return who + " publishes it on intervehicle from the same platform, but intervehicle "
                         "reaches only other platforms";
        case Layer::UNKNOWN: break;
    }
    return who + " publishes it out of reach";
}

std::vector<std::string> diagnose(const Unmatched& sub, const std::vector<Unmatched>& pubs,
                                  bool loose)
{
    std::vector<std::string> reasons;
    if (loose)
        reasons.push_back(
            "only a dynamic publisher whose numeric is set at run time reaches it; "
            "give the numeric with bind:, or count it with validate --assume_dynamic");

    auto sub_name = interface::detail::group_key(sub.entry).name;
    auto sub_layer = interface::layer_name(sub.entry.layer);
    for (const auto& pub : pubs)
    {
        if (pub.entry.is_inner_pub)
            continue;
        auto who = where(endpoint(pub));
        auto pub_layer = interface::layer_name(pub.entry.layer);

        if (pub.entry.unresolved())
        {
            // google::protobuf::Message is published with whatever type it holds at run time
            if (pub.entry.layer == sub.entry.layer &&
                (pub.entry.type == sub.entry.type || pub.entry.type == "google::protobuf::Message"))
                reasons.push_back(who + " publishes " + pub.entry.type + " on " + pub_layer +
                                  " to a group goby_clang_tool could not determine (" +
                                  pub.entry.expr + "); a dynamic: bind can say whether it is " +
                                  sub_name);
            continue;
        }
        if (interface::detail::group_key(pub.entry).name != sub_name)
            continue;

        bool same_layer = pub.entry.layer == sub.entry.layer;
        auto moved = pub.entry;
        moved.layer = sub.entry.layer;
        auto match = interface::connection(moved, sub.entry);

        if (match != GroupMatch::NONE)
        {
            if (!same_layer)
                reasons.push_back(who + " publishes it on " + pub_layer + ", not " + sub_layer);
            else if (match == GroupMatch::EXACT &&
                     !interface::in_scope(pub.entry.layer, pub.at, sub.at))
                reasons.push_back(scope_reason(who, pub));
        }
        else if (interface::group_match(moved, sub.entry) != GroupMatch::NONE)
        {
            reasons.push_back(who + " publishes " + pub.entry.display_group() +
                              (same_layer ? "" : " on " + pub_layer) + " as " + pub.entry.scheme +
                              " " + pub.entry.type);
        }
        else
        {
            reasons.push_back(who + " publishes " + pub.entry.display_group() +
                              (same_layer ? "" : " on " + pub_layer) + ", not " +
                              sub.entry.display_group());
        }
    }
    if (reasons.empty())
        reasons.push_back("nothing in the deployment publishes " + sub_name);
    return reasons;
}
} // namespace

protobuf::InterfaceExplanation explain(const std::string& file,
                                       const interface::Deployment& deployment,
                                       const interface::Resolution& resolution,
                                       const std::string& group_regex,
                                       const std::string& type_regex)
{
    protobuf::InterfaceExplanation out;
    out.set_file(file);
    out.set_group_regex(group_regex);
    if (!type_regex.empty())
        out.set_type_regex(type_regex);

    std::regex group_re(group_regex), type_re(type_regex.empty() ? ".*" : type_regex);
    auto matches = [&](const PubSubEntry& e)
    {
        return (std::regex_search(e.group, group_re) ||
                std::regex_search(e.display_group(), group_re)) &&
               std::regex_search(e.type, type_re);
    };

    std::vector<Unmatched> pubs, subs;
    interface::collect_entries(deployment, pubs, subs);

    for (const auto& sub : subs)
    {
        if (!matches(sub.entry))
            continue;
        auto& s = *out.add_subscription();
        *s.mutable_subscriber() = endpoint(sub);
        if (sub.entry.necessity_overridden)
            s.set_necessity_set_by_deployment(true);

        bool exact = false, loose = false;
        for (const auto& c : resolution.connections)
        {
            if (c.sub_at.application != sub.at.application || !same(c.sub, sub.entry))
                continue;
            *s.add_publisher() = endpoint(c.pub, c.pub_at);
            (c.loose ? loose : exact) = true;
        }
        s.set_met(exact);
        if (!exact)
        {
            for (const auto& reason : diagnose(sub, pubs, loose)) s.add_reason(reason);
        }
    }

    for (const auto& pub : pubs)
    {
        if (pub.entry.is_inner_pub || !matches(pub.entry))
            continue;
        auto& p = *out.add_publication();
        *p.mutable_publisher() = endpoint(pub);
        for (const auto& c : resolution.connections)
        {
            if (c.pub_at.application == pub.at.application && c.pub_at.owner == pub.at.owner &&
                same(c.pub, pub.entry))
                *p.add_subscriber() = endpoint(c.sub, c.sub_at);
        }
        if (pub.entry.unresolved())
            p.add_note("goby_clang_tool could not determine this dynamic group (" + pub.entry.expr +
                       "); a dynamic: bind can say what it stands for");
        else if (pub.entry.unbound())
            p.add_note("its numeric is set at run time; give it with bind:");
        if (p.subscriber_size() == 0)
            p.add_note("no subscriber in its scope");
    }
    return out;
}

std::string render_human(const protobuf::InterfaceExplanation& explanation)
{
    std::stringstream ss;
    if (explanation.subscription_size() == 0 && explanation.publication_size() == 0)
    {
        ss << "nothing in " << explanation.file()
           << " publishes or subscribes to a group matching '" << explanation.group_regex()
           << "'\n";
        return ss.str();
    }

    for (const auto& s : explanation.subscription())
    {
        const auto& sub = s.subscriber();
        ss << "subscription  " << where(sub) << " [" << sub.layer() << "] " << what(sub) << ", "
           << sub.necessity() << (s.necessity_set_by_deployment() ? " (set by the deployment)" : "")
           << ": " << (s.met() ? "met" : "NOT MET") << "\n";
        for (const auto& p : s.publisher())
            ss << "    from " << where(p) << (s.met() ? "" : " (numeric set at run time)") << "\n";
        for (const auto& reason : s.reason()) ss << "    " << reason << "\n";
    }
    for (const auto& p : explanation.publication())
    {
        const auto& pub = p.publisher();
        ss << "publication   " << where(pub) << " [" << pub.layer() << "] " << what(pub) << "\n";
        for (const auto& s : p.subscriber()) ss << "    to " << where(s) << "\n";
        for (const auto& note : p.note()) ss << "    " << note << "\n";
    }
    return ss.str();
}

namespace
{
struct Snapshot
{
    std::set<std::string> applications;
    std::map<std::string, protobuf::InterfaceDiff::Connection> connections;
    std::map<std::string, protobuf::InterfaceIssue> unmet;
};

std::string key(const protobuf::InterfaceEndpoint& e)
{
    return where(e) + " [" + e.layer() + "] " + what(e);
}

Snapshot snapshot(const interface::Deployment& deployment)
{
    Snapshot snap;
    for (const auto& platform : deployment.platforms)
        for (const auto& module : platform.modules)
            for (const auto& app_p : module.applications)
                snap.applications.insert(application_key(platform, module, app_p.second));

    auto resolution = interface::resolve(deployment);
    for (const auto& c : resolution.connections)
    {
        protobuf::InterfaceDiff::Connection connection;
        *connection.mutable_publisher() = endpoint(c.pub, c.pub_at);
        *connection.mutable_subscriber() = endpoint(c.sub, c.sub_at);
        snap.connections.emplace(
            key(connection.publisher()) + " to " + key(connection.subscriber()), connection);
    }

    interface::Issues issues;
    interface::validate(deployment, resolution, interface::ValidateParams(), issues);
    for (const auto& issue : issues)
    {
        if (issue.code != "required-unmet")
            continue;
        protobuf::InterfaceIssue pb;
        to_proto(issue, pb);
        snap.unmet.emplace(key(pb.subject()), pb);
    }
    return snap;
}

template <typename Map, typename Add> void only_in(const Map& in, const Map& not_in, Add add)
{
    for (const auto& kv : in)
    {
        if (!not_in.count(kv.first))
            add(kv.second);
    }
}
} // namespace

protobuf::InterfaceDiff diff(const std::string& file_a, const interface::Deployment& a,
                             const std::string& file_b, const interface::Deployment& b)
{
    protobuf::InterfaceDiff out;
    out.set_file_a(file_a);
    out.set_file_b(file_b);

    auto snap_a = snapshot(a), snap_b = snapshot(b);
    for (const auto& app : snap_b.applications)
        if (!snap_a.applications.count(app))
            out.add_application_added(app);
    for (const auto& app : snap_a.applications)
        if (!snap_b.applications.count(app))
            out.add_application_removed(app);
    only_in(snap_b.connections, snap_a.connections,
            [&](const protobuf::InterfaceDiff::Connection& c) { *out.add_connection_added() = c; });
    only_in(snap_a.connections, snap_b.connections,
            [&](const protobuf::InterfaceDiff::Connection& c)
            { *out.add_connection_removed() = c; });
    only_in(snap_b.unmet, snap_a.unmet,
            [&](const protobuf::InterfaceIssue& i) { *out.add_newly_unmet() = i; });
    only_in(snap_a.unmet, snap_b.unmet,
            [&](const protobuf::InterfaceIssue& i) { *out.add_newly_met() = i; });
    return out;
}

bool has_differences(const protobuf::InterfaceDiff& d)
{
    return d.application_added_size() || d.application_removed_size() ||
           d.connection_added_size() || d.connection_removed_size() || d.newly_unmet_size() ||
           d.newly_met_size();
}

std::string render_human(const protobuf::InterfaceDiff& d)
{
    std::stringstream ss;
    ss << d.file_a() << " vs " << d.file_b() << ": ";
    if (!has_differences(d))
    {
        ss << "no differences\n";
        return ss.str();
    }

    std::vector<std::string> parts;
    auto count = [&](int n, const std::string& word, const std::string& what_happened)
    {
        if (n)
            parts.push_back(plural(n, word) + " " + what_happened);
    };
    count(d.application_added_size(), "application", "added");
    count(d.application_removed_size(), "application", "removed");
    count(d.connection_added_size(), "connection", "added");
    count(d.connection_removed_size(), "connection", "removed");
    count(d.newly_unmet_size(), "required subscription", "newly unmet");
    count(d.newly_met_size(), "required subscription", "newly met");
    for (std::size_t i = 0; i < parts.size(); ++i) ss << (i ? ", " : "") << parts[i];
    ss << "\n";

    auto connection = [](const protobuf::InterfaceDiff::Connection& c)
    {
        return "[" + c.publisher().layer() + "] " + where(c.publisher()) + " to " +
               where(c.subscriber()) + ": " + what(c.publisher());
    };
    for (const auto& app : d.application_added()) ss << "  + application  " << app << "\n";
    for (const auto& app : d.application_removed()) ss << "  - application  " << app << "\n";
    for (const auto& c : d.connection_added()) ss << "  + connection   " << connection(c) << "\n";
    for (const auto& c : d.connection_removed()) ss << "  - connection   " << connection(c) << "\n";
    for (const auto& i : d.newly_unmet())
        ss << "  ! unmet        " << where(i.subject()) << " [" << i.subject().layer() << "] "
           << i.message() << "\n";
    for (const auto& i : d.newly_met())
        ss << "  * met          " << where(i.subject()) << " [" << i.subject().layer() << "] "
           << what(i.subject()) << "\n";
    return ss.str();
}

std::string init(const std::string& directory, const std::string& platform,
                 const std::string& deployment_name, int& applications)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
        auto ext = it->path().extension().string();
        if (ext == ".yml" || ext == ".yaml")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    std::set<std::string> names;
    for (const auto& file : files)
    {
        auto application = interface::detail::declared_application(file.string());
        if (!application.empty())
            names.insert(application);
    }
    applications = names.size();

    auto name = deployment_name;
    if (name.empty())
    {
        auto dir = fs::path(directory).lexically_normal();
        name = dir.filename().empty() ? dir.parent_path().filename().string()
                                      : dir.filename().string();
        if (name.empty() || name == ".")
            name = fs::current_path().filename().string();
    }

    auto scalar = [](const std::string& s)
    {
        static const std::regex plain("[A-Za-z0-9_][A-Za-z0-9_:.\\-]*");
        return std::regex_match(s, plain) ? s : "\"" + s + "\"";
    };

    std::stringstream ss;
    ss << "# Written by 'goby interface init " << directory << "': every application found there, "
       << "on one platform.\n"
       << "# Split it into platforms (and modules, one per gobyd), give a platform instances: "
          "for each vehicle,\n"
       << "# and list plugins under their host's plugins:. Interface files are found beside this "
          "file, or with -I.\n"
       << "deployment: " << scalar(name) << "\n"
       << "platforms:\n"
       << "  - name: " << scalar(platform) << "\n"
       << "    run:\n";
    for (const auto& application : names) ss << "      - " << scalar(application) << "\n";
    return ss.str();
}

} // namespace interface_tool
} // namespace middleware
} // namespace apps
} // namespace goby
