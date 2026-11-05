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

/// \brief Match every publication to the subscriptions it reaches
inline Resolution resolve(const Deployment& deployment)
{
    std::vector<Unmatched> pubs, subs;
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

    Resolution resolution;
    std::vector<bool> sub_matched(subs.size(), false);
    for (const auto& pub : pubs)
    {
        bool pub_matched = false;
        for (std::size_t i = 0, n = subs.size(); i < n; ++i)
        {
            const auto& sub = subs[i];
            if (sub.entry.layer == pub.entry.layer && in_scope(pub.entry.layer, pub.at, sub.at) &&
                connects(pub.entry, sub.entry))
            {
                resolution.connections.push_back({pub.entry, pub.at, sub.entry, sub.at});
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
            "goby::ThreadJoinable",
            "goby::ThreadShutdown",
            "goby::middleware::intervehicle::modem_data_in",
            "goby::middleware::intervehicle::modem_data_out",
            "goby::middleware::intervehicle::metadata_request",
            "goby::middleware::intervehicle::modem_ack_in",
            "goby::middleware::intervehicle::modem_expire_in",
            "goby::middleware::intervehicle::modem_subscription_forward_tx"};
        static const std::set<std::string> terminate_groups{"goby::terminate::request",
                                                            "goby::terminate::response"};
        static const std::set<std::string> coroner_groups{"goby::health::request",
                                                          "goby::health::response"};

        if (!params_.omit_group_regex.empty() && std::regex_match(group, omit_group_regex_))
            return false;
        if (internal_groups.count(group) && !params_.include_internal)
            return false;
        if (terminate_groups.count(group) && !params_.include_terminate)
            return false;
        if (coroner_groups.count(group) && !params_.include_coroner)
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

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
