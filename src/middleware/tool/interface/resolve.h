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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_RESOLVE_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_RESOLVE_H

#include <map>
#include <regex>
#include <set>
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
/// \brief Where in a deployment a publication or subscription is made
struct Endpoint
{
    const Platform* platform{nullptr};
    const Module* module{nullptr};
    const Application* application{nullptr};
    // the Thread whose interface lists the entry (interthread only); a base class's entries are
    // drawn as its most derived class but still belong to the base Thread
    const Thread* owner{nullptr};
};

struct Connection
{
    PubSubEntry pub;
    Endpoint pub_at;
    PubSubEntry sub;
    Endpoint sub_at;
    // made only by assuming an unbound dynamic group's run-time numeric matches
    bool loose{false};
};

struct Unmatched
{
    PubSubEntry entry;
    Endpoint at;
};

struct Resolution
{
    std::vector<Connection> connections;
    // publications that no subscription in scope receives
    std::vector<Unmatched> unsubscribed;
    // subscriptions that no publication in scope reaches
    std::vector<Unmatched> unpublished;
};

/// \brief Can a publication made at \c pub reach a subscription made at \c sub on \c layer?
///
/// Interprocess stops at the module (one gobyd), intermodule at the platform, and intervehicle
/// reaches every other platform.
inline bool in_scope(Layer layer, const Endpoint& pub, const Endpoint& sub)
{
    switch (layer)
    {
        case Layer::INTERTHREAD: return pub.application == sub.application;
        case Layer::INTERPROCESS: return pub.module == sub.module;
        case Layer::INTERMODULE: return pub.platform == sub.platform;
        case Layer::INTERVEHICLE: return pub.platform != sub.platform;
        case Layer::UNKNOWN: break;
    }
    return false;
}

/// \brief Every publication and subscription in a deployment, with where it is made
inline void collect_entries(const Deployment& deployment, std::vector<Unmatched>& pubs,
                            std::vector<Unmatched>& subs)
{
    for (const auto& platform : deployment.platforms)
    {
        for (const auto& module : platform.modules)
        {
            for (const auto& app_p : module.applications)
            {
                const auto& app = app_p.second;
                for (const auto& thread_p : app.threads)
                {
                    Endpoint at{&platform, &module, &app, thread_p.second.get()};
                    for (const auto& e : thread_p.second->interthread_publishes)
                        pubs.push_back({e, at});
                    for (const auto& e : thread_p.second->interthread_subscribes)
                        subs.push_back({e, at});
                }

                Endpoint at{&platform, &module, &app, nullptr};
                for (auto layer : {Layer::INTERPROCESS, Layer::INTERMODULE, Layer::INTERVEHICLE})
                {
                    for (const auto& e : app.publishes_on(layer)) pubs.push_back({e, at});
                    for (const auto& e : app.subscribes_on(layer)) subs.push_back({e, at});
                }
            }
        }
    }
}

/// \brief Match every publication to the subscriptions it reaches
inline Resolution resolve(const Deployment& deployment)
{
    std::vector<Unmatched> pubs, subs;
    collect_entries(deployment, pubs, subs);

    Resolution resolution;
    std::vector<bool> sub_matched(subs.size(), false);
    for (const auto& pub : pubs)
    {
        bool pub_matched = false;
        for (std::size_t i = 0, n = subs.size(); i < n; ++i)
        {
            const auto& sub = subs[i];
            if (sub.entry.layer != pub.entry.layer || !in_scope(pub.entry.layer, pub.at, sub.at))
                continue;
            auto match = connection(pub.entry, sub.entry);
            if (match != GroupMatch::NONE)
            {
                resolution.connections.push_back(
                    {pub.entry, pub.at, sub.entry, sub.at, match == GroupMatch::LOOSE});
                pub_matched = true;
                sub_matched[i] = true;
            }
        }
        if (!pub_matched)
            resolution.unsubscribed.push_back(pub);
    }
    for (std::size_t i = 0, n = subs.size(); i < n; ++i)
    {
        if (!sub_matched[i])
            resolution.unpublished.push_back(subs[i]);
    }
    return resolution;
}

/// \brief Which groups and nodes a diagram (or a filtered check) shows
class ViewFilter
{
  public:
    struct Params
    {
        std::string omit_group_regex;
        std::string omit_node_regex;
        bool include_coroner{false};
        bool include_terminate{false};
        bool include_internal{false};
        bool omit_interthread{false};
        // draw Goby's own applications (gobyd, goby_*) with their threads
        bool expand_goby_apps{false};
    };

    ViewFilter(Params params)
        : params_(std::move(params)),
          omit_group_regex_(params_.omit_group_regex),
          omit_node_regex_(params_.omit_node_regex)
    {
    }

    const Params& params() const { return params_; }

    bool is_group_included(const std::string& group) const
    {
        static const std::set<std::string> internal_groups{
            "goby::middleware::interprocess::to_portal",
            "goby::middleware::interprocess::regex",
            "goby::middleware::SerializationUnSubscribeAll",
            "goby::middleware::Thread::joinable",
            "goby::middleware::Thread::shutdown",
            "goby::configuration",
            "goby::datum_update",
            "goby::middleware::intervehicle::modem_data_in",
            "goby::middleware::intervehicle::modem_data_out",
            "goby::middleware::intervehicle::metadata_request",
            "goby::middleware::intervehicle::modem_ack_in",
            "goby::middleware::intervehicle::modem_expire_in",
            "goby::middleware::intervehicle::modem_subscription_forward_tx"};
        static const std::set<std::string> terminate_groups{
            "goby::terminate::request", "goby::terminate::response", "goby::terminate::result"};
        static const std::set<std::string> coroner_groups{
            "goby::health::request", "goby::health::response", "goby::health::report"};

        if (!params_.omit_group_regex.empty() && std::regex_match(group, omit_group_regex_))
            return false;
        // with or without a numeric
        auto name = group.substr(0, group.find(';'));
        if (internal_groups.count(name) && !params_.include_internal)
            return false;
        if (terminate_groups.count(name) && !params_.include_terminate)
            return false;
        if (coroner_groups.count(name) && !params_.include_coroner)
            return false;
        return true;
    }

    bool is_node_included(const std::string& node) const
    {
        return params_.omit_node_regex.empty() || !std::regex_match(node, omit_node_regex_);
    }

    /// \brief With omit_interthread, a thread is shown only if it uses an outer layer
    bool is_thread_included(const Application& app, const Thread& thread) const
    {
        if (!params_.omit_interthread)
            return true;
        auto name = thread.most_derived_name();
        for (const auto* layer_map : {&app.publishes, &app.subscribes})
        {
            for (const auto& layer_p : *layer_map)
            {
                for (const auto& e : layer_p.second)
                {
                    if (e.thread == name)
                        return true;
                }
            }
        }
        return false;
    }

    /// \brief Is every part of the path to this endpoint (and its thread) shown?
    bool is_included(const Endpoint& at, const std::string& thread) const
    {
        if (!is_node_included(at.platform->name))
            return false;
        if (at.platform->has_modules() && !is_node_included(at.module->name))
            return false;
        if (!is_node_included(at.application->name) || !is_node_included(thread))
            return false;
        auto thread_it = at.application->threads.find(thread);
        return thread_it == at.application->threads.end() ||
               is_thread_included(*at.application, *thread_it->second);
    }

    bool is_included(const Connection& c) const
    {
        if (params_.omit_interthread && c.pub.layer == Layer::INTERTHREAD)
            return false;
        return is_group_included(c.pub.group) && is_group_included(c.sub.group) &&
               is_included(c.pub_at, c.pub.thread) && is_included(c.sub_at, c.sub.thread);
    }

    bool is_included(const Unmatched& u) const
    {
        if (params_.omit_interthread && u.entry.layer == Layer::INTERTHREAD)
            return false;
        return is_group_included(u.entry.group) && is_included(u.at, u.entry.thread);
    }

  private:
    Params params_;
    std::regex omit_group_regex_;
    std::regex omit_node_regex_;
};

/// \brief Goby's own applications, which a diagram draws as one node without their threads
inline bool is_goby_application(const Application& app)
{
    return app.name == "gobyd" || app.name.rfind("goby_", 0) == 0;
}

/// \brief A thread from Goby itself: a goby:: class, or a lambda in one of Goby's headers
inline bool is_goby_thread(const std::string& name)
{
    static const std::regex goby_lambda(R"(\(lambda at .*/goby/.*\))");
    return name.rfind("goby::", 0) == 0 || std::regex_match(name, goby_lambda);
}

/// \brief A thread's name as drawn: a lambda by its file and line, a template without its arguments
inline std::string display_thread_name(const std::string& name)
{
    static const std::regex lambda(R"(\(lambda at (?:.*/)?([^/]+):(\d+):\d+\))");
    std::smatch match;
    if (std::regex_match(name, match, lambda))
        return "lambda (" + match[1].str() + ":" + match[2].str() + ")";
    auto lt = name.find('<');
    return lt == std::string::npos ? name : name.substr(0, lt) + "<...>";
}

/// \brief What a diagram draws of a resolved deployment, shared by the DOT and D2 writers
class DiagramView
{
  public:
    DiagramView(const Deployment& deployment, const Resolution& resolution, ViewFilter filter,
                bool omit_disconnected)
        : filter_(std::move(filter)), omit_disconnected_(omit_disconnected)
    {
        // a deployment of only Goby's applications (such as one's interface file) is about them
        for (const auto& platform : deployment.platforms)
        {
            for (const auto& module : platform.modules)
            {
                for (const auto& app_p : module.applications)
                    collapse_goby_apps_ |= !is_goby_application(app_p.second);
            }
        }
        collapse_goby_apps_ &= !filter_.params().expand_goby_apps;

        auto use = [&](const Endpoint& at, const std::string& thread)
        { used_[at.application].insert(thread); };
        for (const auto& c : resolution.connections)
        {
            if (drawn(c))
            {
                use(c.pub_at, c.pub.thread);
                use(c.sub_at, c.sub.thread);
            }
        }
        for (const auto& u : resolution.unsubscribed)
        {
            if (drawn_unsubscribed(u))
                use(u.at, u.entry.thread);
        }
        for (const auto& u : resolution.unpublished)
        {
            if (drawn_unpublished(u))
                use(u.at, u.entry.thread);
        }

        // gobyd is the module (its interprocess boundary), so unless something is drawn to it,
        // it is shown by naming it on its container
        for (const auto& platform : deployment.platforms)
        {
            for (const auto& module : platform.modules)
            {
                for (const auto& app_p : module.applications)
                {
                    const auto& app = app_p.second;
                    if (app.name == "gobyd" && collapsed(app) && !used_.count(&app))
                        folded_.insert(&app);
                }
            }
        }
    }

    const ViewFilter& filter() const { return filter_; }

    bool collapsed(const Application& app) const
    {
        return collapse_goby_apps_ && is_goby_application(app);
    }

    bool folded(const Application& app) const { return folded_.count(&app); }

    /// \brief The container's name, with any gobyd folded into it
    std::string container_label(const std::string& name, const Module& module) const
    {
        for (const auto& app_p : module.applications)
        {
            if (folded(app_p.second))
                return name + " (gobyd)";
        }
        return name;
    }

    bool drawn(const Connection& c) const
    {
        if (c.pub.layer == Layer::INTERTHREAD && collapsed(*c.pub_at.application))
            return false;
        return filter_.is_included(c);
    }

    bool drawn_unsubscribed(const Unmatched& u) const
    {
        return !omit_disconnected_ && !u.entry.is_inner_pub && !collapsed(*u.at.application) &&
               !goby_internal(u) && filter_.is_included(u);
    }

    /// \brief An unmet required subscription is drawn even when the rest are left out
    bool drawn_unpublished(const Unmatched& u) const
    {
        bool required = u.entry.necessity == goby::middleware::Necessity::REQUIRED;
        if ((collapsed(*u.at.application) || goby_internal(u)) &&
            (!required || u.entry.layer == Layer::INTERTHREAD))
            return false;
        if (omit_disconnected_ && !required)
            return false;
        return filter_.is_included(u);
    }

    /// \brief Goby's own threads within a project's application (such as its portal's forwarder)
    /// are drawn only when something is drawn to them
    bool thread_drawn(const Application& app, const Thread& thread) const
    {
        auto name = thread.most_derived_name();
        if (!filter_.is_node_included(name) || !filter_.is_thread_included(app, thread))
            return false;
        if (is_goby_application(app) || !is_goby_thread(name))
            return true;
        auto it = used_.find(&app);
        return it != used_.end() && it->second.count(name);
    }

  private:
    // made by Goby's own thread within a project's application
    bool goby_internal(const Unmatched& u) const
    {
        return !is_goby_application(*u.at.application) && is_goby_thread(u.entry.thread);
    }

    ViewFilter filter_;
    bool omit_disconnected_;
    bool collapse_goby_apps_{false};
    std::map<const Application*, std::set<std::string>> used_;
    std::set<const Application*> folded_;
};

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
