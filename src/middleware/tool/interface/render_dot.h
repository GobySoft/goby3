// Copyright 2020-2026:
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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_RENDER_DOT_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_RENDER_DOT_H

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>

#include "goby/middleware/tool/interface/resolve.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
namespace style
{
// Graphviz X11 names: darkgreen, orange3, dodgerblue4, purple4
const std::string vehicle_color = "#006400";
const std::string module_color = "#cd8500";
const std::string process_color = "#104e8b";
const std::string thread_color = "#551a8b";

inline const std::string& layer_color(Layer layer)
{
    switch (layer)
    {
        case Layer::INTERTHREAD: return thread_color;
        case Layer::INTERMODULE: return module_color;
        case Layer::INTERVEHICLE: return vehicle_color;
        case Layer::UNKNOWN:
        case Layer::INTERPROCESS: break;
    }
    return process_color;
}
} // namespace style

struct DotParams
{
    ViewFilter::Params filter;
    bool omit_disconnected{false};
    std::string splines{"ortho"};
};

namespace detail
{
inline void html_escape(std::string& s, bool do_break_before_lt = true,
                        bool do_break_after_commas = true)
{
    using boost::algorithm::replace_all;
    replace_all(s, "&", "&amp;");
    replace_all(s, "\"", "&quot;");
    replace_all(s, "\'", "&apos;");
    replace_all(s, "<", "&lt;");
    replace_all(s, ">", "&gt;");

    if (do_break_before_lt)
    {
        boost::algorithm::replace_first(s, "&lt;", "<br/>&lt;");
        replace_all(s, "&lt;", "<font point-size=\"8\">&lt;");
        replace_all(s, "&gt;", "&gt;</font>");
    }

    if (do_break_after_commas)
        replace_all(s, ", ", ",<br/>");
}

inline std::string html_escaped(std::string s, bool break_before_lt = true,
                                bool break_after_commas = true)
{
    html_escape(s, break_before_lt, break_after_commas);
    return s;
}

// unquoted DOT IDs may only contain letters, digits and underscores
inline std::string dot_id(const std::string& s)
{
    std::string escaped;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '_')
            escaped += c;
        else
            escaped += "_" + std::to_string(static_cast<int>(c)) + "_";
    }
    return escaped;
}

inline std::string dot_quoted(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

inline std::string short_group(const std::string& group)
{
    auto last_colon = group.find_last_of(":");
    return (last_colon == std::string::npos) ? group : group.substr(last_colon + 1);
}

class DotWriter
{
  public:
    DotWriter(std::ostream& os, const Deployment& deployment, const Resolution& resolution,
              const DotParams& params)
        : os_(os),
          deployment_(deployment),
          resolution_(resolution),
          params_(params),
          filter_(params.filter),
          indexer_(deployment.indexer)
    {
    }

    void write();

  private:
    std::string node_name(const Endpoint& at, const std::string& thread) const
    {
        return dot_id(at.platform->name) + "_" + dot_id(at.module->name) + "_" +
               dot_id(at.application->name) + "_" + dot_id(thread);
    }

    void open_cluster(const std::string& indent, const std::string& name, const std::string& color)
    {
        os_ << indent.substr(1) << "subgraph cluster_" << cluster_++ << " {\n";
        os_ << indent << "label=<<b>" << html_escaped(name) << "</b>>\n";
        os_ << indent << "fontcolor=\"" << color << "\"\n";
        os_ << indent << "penwidth=2\n";
        os_ << indent << "pencolor=\"" << color << box_transparency_ << "\"\n";
    }

    std::string edge(const PubSubEntry& pub, const std::string& pub_node,
                     const std::string& sub_node, const std::string& color,
                     goby::middleware::Necessity necessity, bool is_regex)
    {
        pubs_in_use_[pub_node][pub.layer].emplace(pub.publish_index, pub);

        std::string edge_style;
        switch (necessity)
        {
            case goby::middleware::Necessity::REQUIRED: edge_style = "bold"; break;
            case goby::middleware::Necessity::RECOMMENDED: edge_style = "tapered"; break;
            case goby::middleware::Necessity::OPTIONAL: edge_style = "solid"; break;
        }
        if (is_regex)
            edge_style = "dotted";

        auto label = pub.publish_index_str();
        auto tooltip = dot_quoted(label + ": " + pub.group + " | " + pub.scheme + " | " + pub.type);
        return pub_node + "->" + sub_node + "[fontsize=7,headlabel=\"" + label + "\",taillabel=\"" +
               label + "\",xlabel=<" + label + "[" +
               html_escaped(short_group(pub.group), false, false) + "]>,color=\"" + color +
               "\",style=" + edge_style + ",tooltip=\"" + tooltip + "\",labeltooltip=\"" + tooltip +
               "\",headtooltip=\"" + tooltip + "\",penwidth=0.5,arrowhead=vee,arrowsize=0.3]\n";
    }

    void connection(const std::string& indent, const Connection& c)
    {
        os_ << indent
            << edge(c.pub, node_name(c.pub_at, c.pub.thread), node_name(c.sub_at, c.sub.thread),
                    style::layer_color(c.pub.layer), c.sub.necessity, c.sub.is_regex);
    }

    void unsubscribed(const std::string& indent, const Unmatched& u)
    {
        if (params_.omit_disconnected || u.entry.is_inner_pub)
            return;
        const auto& color = style::layer_color(u.entry.layer);
        auto node = node_name(u.at, u.entry.thread);
        auto placeholder = node + "_no_subscribers_" + dot_id(color);
        os_ << indent << placeholder << " [label=\"\",style=invis] \n"
            << edge(u.entry, node, placeholder, color, goby::middleware::Necessity::OPTIONAL,
                    false);
    }

    void unpublished(const std::string& indent, const Unmatched& u)
    {
        bool required = u.entry.necessity == goby::middleware::Necessity::REQUIRED;
        if (params_.omit_disconnected && !required)
            return;
        std::string color = required ? "red" : style::layer_color(u.entry.layer);
        auto node = node_name(u.at, u.entry.thread);
        auto placeholder = node + "_no_publishers_" + dot_id(color);

        node_threads_[placeholder] = nullptr;
        PubSubEntry fake_pub = u.entry;
        fake_pub.direction = PubSubEntry::Direction::PUBLISH;
        fake_pub.publish_index = indexer_.next(fake_pub.layer);

        os_ << indent << placeholder << "  \n"
            << edge(fake_pub, placeholder, node, color, u.entry.necessity, u.entry.is_regex);
    }

    void connections_from(const std::string& indent, Layer layer,
                          const std::function<bool(const Endpoint&)>& at)
    {
        for (const auto& c : resolution_.connections)
        {
            if (c.pub.layer == layer && at(c.pub_at) && filter_.is_included(c))
                connection(indent, c);
        }
        for (const auto& u : resolution_.unsubscribed)
        {
            if (u.entry.layer == layer && at(u.at) && filter_.is_included(u))
                unsubscribed(indent, u);
        }
    }

    void unpublished_in(const std::string& indent, Layer layer,
                        const std::function<bool(const Endpoint&)>& at)
    {
        std::vector<const Unmatched*> subs;
        for (const auto& u : resolution_.unpublished)
        {
            if (u.entry.layer == layer && at(u.at) && filter_.is_included(u))
                subs.push_back(&u);
        }
        std::stable_sort(subs.begin(), subs.end(),
                         [](const Unmatched* a, const Unmatched* b)
                         {
                             auto key = [](const Unmatched* u) {
                                 return std::make_tuple(u->at.platform->name, u->at.module->name,
                                                        u->at.application->name);
                             };
                             if (key(a) != key(b))
                                 return key(a) < key(b);
                             return a->entry < b->entry;
                         });
        for (const auto* u : subs) unpublished(indent, *u);
    }

    void write_node_labels();

  private:
    std::ostream& os_;
    const Deployment& deployment_;
    const Resolution& resolution_;
    const DotParams& params_;
    ViewFilter filter_;
    PublishIndexer indexer_;
    int cluster_{0};
    const std::string box_transparency_{"20"};

    // node -> thread drawn there (nullptr for an unpublished subscription's placeholder)
    std::map<std::string, const Thread*> node_threads_;
    // node -> layer -> publish index -> publication
    std::map<std::string, std::map<Layer, std::map<int, PubSubEntry>>> pubs_in_use_;
};

inline void DotWriter::write()
{
    os_ << "digraph " << dot_id(deployment_.name) << " { \n";
    os_ << "\tsplines=" << params_.splines << "\n";
    os_ << "\tnewrank=true\n";

    for (const auto& platform : deployment_.platforms)
    {
        if (!filter_.is_node_included(platform.name))
            continue;
        open_cluster("\t\t", platform.name, style::vehicle_color);

        for (const auto& module : platform.modules)
        {
            if (platform.has_modules())
            {
                if (!filter_.is_node_included(module.name))
                    continue;
                open_cluster("\t\t", module.name, style::module_color);
            }

            for (const auto& app_p : module.applications)
            {
                const auto& app = app_p.second;
                if (!filter_.is_node_included(app.name))
                    continue;
                open_cluster("\t\t\t", app.name, style::process_color);

                for (const auto& thread_p : app.threads)
                {
                    const auto& thread = *thread_p.second;
                    if (!filter_.is_node_included(thread.most_derived_name()) ||
                        !filter_.is_thread_included(app, thread))
                        continue;

                    connections_from("\t\t\t", Layer::INTERTHREAD,
                                     [&](const Endpoint& at) { return at.owner == &thread; });

                    Endpoint at{&platform, &module, &app, &thread};
                    auto node = node_name(at, thread.most_derived_name());
                    node_threads_[node] = &thread;
                    os_ << "\t\t\t" << node << "[penwidth=2,color=\"" << style::thread_color
                        << box_transparency_ << "\"]\n";
                }

                unpublished_in("\t\t\t", Layer::INTERTHREAD,
                               [&](const Endpoint& at) { return at.application == &app; });
                os_ << "\t\t}\n";

                connections_from("\t\t", Layer::INTERPROCESS,
                                 [&](const Endpoint& at) { return at.application == &app; });
            }

            unpublished_in("\t\t", Layer::INTERPROCESS,
                           [&](const Endpoint& at) { return at.module == &module; });

            if (platform.has_modules())
                os_ << "\t}\n";

            connections_from("\t", Layer::INTERMODULE,
                             [&](const Endpoint& at) { return at.module == &module; });
        }

        unpublished_in("\t\t", Layer::INTERMODULE,
                       [&](const Endpoint& at) { return at.platform == &platform; });
        os_ << "\t}\n";

        connections_from("\t", Layer::INTERVEHICLE,
                         [&](const Endpoint& at) { return at.platform == &platform; });
    }

    unpublished_in("\t", Layer::INTERVEHICLE, [](const Endpoint&) { return true; });

    write_node_labels();
    os_ << "}\n";
}

inline void DotWriter::write_node_labels()
{
    for (const auto& node_thread_p : node_threads_)
    {
        const auto& node = node_thread_p.first;
        const auto* thread = node_thread_p.second;

        std::string pub_key;
        for (const auto& layer_p : pubs_in_use_[node])
        {
            for (const auto& index_pub_p : layer_p.second)
            {
                const auto& pub = index_pub_p.second;
                pub_key += "<font color=\"" + style::layer_color(layer_p.first) +
                           "\" point-size=\"10\">" + pub.publish_index_str() +
                           ": </font><b><font point-size=\"10\">" + html_escaped(pub.group) +
                           "</font></b><br/><font point-size=\"6\">" + html_escaped(pub.scheme) +
                           "</font><br/><font point-size=\"8\">" +
                           html_escaped(pub.type, false, false) + "</font><br/>";
            }
        }

        if (thread)
        {
            os_ << "\t" << node << "\n"
                << " [label=<<font color=\"" << style::thread_color << "\">"
                << html_escaped(thread->most_derived_name()) << "</font><br/>" << pub_key
                << ">,shape=box,style=" << (thread->known ? "solid" : "dashed") << "]\n";
        }
        else
        {
            os_ << "\t" << node << "\n"
                << " [label=<" << pub_key << ">,penwidth=0]\n";
        }
    }
}
} // namespace detail

/// \brief Write a Graphviz digraph of the deployment's publish/subscribe connections
inline void write_dot(std::ostream& os, const Deployment& deployment, const Resolution& resolution,
                      const DotParams& params)
{
    detail::DotWriter(os, deployment, resolution, params).write();
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
