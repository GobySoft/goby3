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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_RENDER_D2_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_RENDER_D2_H

#include <cctype>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "goby/middleware/tool/interface/render_dot.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
struct D2Params
{
    ViewFilter::Params filter;
    bool omit_disconnected{false};
    // elk lays out connections between nested containers better than d2's default, dagre
    std::string layout_engine{"elk"};
};

namespace detail
{
inline std::string d2_quoted(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '\n')
        {
            out += "\\n";
            continue;
        }
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

// d2 renders a tooltip as Markdown and parses the HTML, so a template argument list such as
// <class Foo> would be read as a tag
inline std::string d2_tooltip(const std::string& s)
{
    std::string escaped;
    for (char c : s)
    {
        if (c == '<')
            escaped += "&lt;";
        else if (c == '>')
            escaped += "&gt;";
        else
            escaped += c;
    }
    return d2_quoted(escaped);
}

/// \brief Writes a deployment as D2 (https://d2lang.com): the same containers, colors and edge
/// conventions as the DOT output, with styles defined once as classes
class D2Writer
{
  public:
    D2Writer(std::ostream& os, const Deployment& deployment, const Resolution& resolution,
             const D2Params& params)
        : os_(os),
          deployment_(deployment),
          resolution_(resolution),
          params_(params),
          filter_(params.filter),
          view_(deployment, resolution, filter_, params.omit_disconnected),
          indexer_(deployment.indexer)
    {
    }

    void write()
    {
        collect();
        write_header();
        write_containers();
        write_edges();
    }

  private:
    struct Edge
    {
        std::string from;
        std::string to;
        PubSubEntry pub;
        std::vector<std::string> classes;
    };

    // D2 keys: letters, digits and underscores, unique among their siblings, never a keyword
    std::string id(const std::string& parent, const std::string& name)
    {
        auto key = parent + "\n" + name;
        auto it = ids_.find(key);
        if (it != ids_.end())
            return it->second;

        static const std::set<std::string> keywords{
            "label",        "style",     "class", "classes",          "shape",
            "icon",         "near",      "width", "height",           "tooltip",
            "link",         "direction", "vars",  "constraint",       "grid-rows",
            "grid-columns", "top",       "left",  "source-arrowhead", "target-arrowhead",
            "layers",       "scenarios", "steps", "filled",           "_"};
        // keyed by the unqualified name without template arguments; the label keeps the rest
        auto shortened = name.substr(0, name.find('<'));
        auto scope = shortened.rfind("::");
        if (scope != std::string::npos)
            shortened = shortened.substr(scope + 2);

        std::string base;
        for (unsigned char c : shortened)
        {
            if (std::isalnum(c) || c == '_')
                base += c;
            else if (base.empty() || base.back() != '_')
                base += '_';
        }
        while (!base.empty() && base.back() == '_') base.pop_back();
        while (!base.empty() && base.front() == '_') base.erase(0, 1);
        if (base.empty() || keywords.count(base))
            base = "n_" + base;

        std::string unique = base;
        for (int n = 2; used_[parent].count(unique); ++n) unique = base + "_" + std::to_string(n);
        used_[parent].insert(unique);
        return ids_[key] = unique;
    }

    // a collapsed application is one shape, whichever of its threads an entry names
    std::string path(const Endpoint& at, const std::string& thread)
    {
        auto app = app_path(at);
        if (view_.collapsed(*at.application))
            return app;
        threads_[app].insert(thread);
        return app + "." + id(app, thread);
    }

    std::string app_path(const Endpoint& at)
    {
        std::string p = id("", at.platform->name);
        if (at.platform->has_modules())
            p += "." + id(p, at.module->name);
        return p + "." + id(p, at.application->name);
    }

    // the "no publisher" / "no subscriber" node beside an application's threads
    std::string placeholder_id(const std::string& app, bool no_publisher)
    {
        // the leading control character keeps these apart from a thread of the same name
        auto key = no_publisher ? "\x01no publisher" : "\x01no subscriber";
        auto placeholder = id(app, key);
        placeholders_[app][placeholder] = no_publisher ? "no publisher" : "no subscriber";
        return placeholder;
    }

    std::vector<std::string> edge_classes(const PubSubEntry& sub, bool dynamic)
    {
        std::vector<std::string> classes{layer_name(sub.layer)};
        if (sub.necessity == goby::middleware::Necessity::REQUIRED)
            classes.push_back("required");
        else if (sub.necessity == goby::middleware::Necessity::RECOMMENDED)
            classes.push_back("recommended");
        if (sub.is_regex)
            classes.push_back("regex");
        else if (dynamic)
            classes.push_back("dynamic");
        return classes;
    }

    void collect()
    {
        for (const auto& c : resolution_.connections)
        {
            if (!view_.drawn(c))
                continue;
            edges_.push_back({path(c.pub_at, c.pub.thread), path(c.sub_at, c.sub.thread), c.pub,
                              edge_classes(c.sub, c.pub.dynamic || c.sub.dynamic || c.loose)});
        }

        for (const auto& u : resolution_.unsubscribed)
        {
            if (!view_.drawn_unsubscribed(u))
                continue;
            auto app = app_path(u.at);
            auto placeholder = placeholder_id(app, false);
            std::vector<std::string> classes{layer_name(u.entry.layer)};
            if (u.entry.dynamic)
                classes.push_back("dynamic");
            edges_.push_back(
                {path(u.at, u.entry.thread), app + "." + placeholder, u.entry, classes});
        }

        for (const auto& u : resolution_.unpublished)
        {
            if (!view_.drawn_unpublished(u))
                continue;
            bool required = u.entry.necessity == goby::middleware::Necessity::REQUIRED;
            auto app = app_path(u.at);
            auto placeholder = placeholder_id(app, true);
            PubSubEntry placeholder_pub = u.entry;
            placeholder_pub.publish_index = indexer_.next(u.entry.layer);
            auto classes = edge_classes(u.entry, u.entry.dynamic);
            if (required)
                classes.push_back("unmet");
            edges_.push_back(
                {app + "." + placeholder, path(u.at, u.entry.thread), placeholder_pub, classes});
        }

        for (const auto& e : edges_)
        {
            if (e.pub.direction == PubSubEntry::Direction::PUBLISH)
                pubs_by_thread_[e.from].emplace(e.pub.layer, e.pub.publish_index, e.pub);
        }
    }

    void write_header()
    {
        if (!params_.layout_engine.empty())
            os_ << "vars: { d2-config: { layout-engine: " << params_.layout_engine << " } }\n";
        os_ << "direction: right\n";
        os_ << "classes: {\n";
        auto container = [&](const std::string& name, const std::string& color)
        {
            os_ << "  " << name << ": { style.stroke: \"" << color << "\"; style.font-color: \""
                << color << "\"; style.stroke-width: 2; style.fill: transparent }\n";
        };
        container("platform", style::vehicle_color);
        container("module", style::module_color);
        container("application", style::process_color);
        os_ << "  thread: { style.stroke: \"" << style::thread_color << "\"; style.font-color: \""
            << style::thread_color << "\" }\n";
        os_ << "  placeholder: { shape: text; style.font-color: \"#888888\" }\n";
        for (auto layer :
             {Layer::INTERTHREAD, Layer::INTERPROCESS, Layer::INTERMODULE, Layer::INTERVEHICLE})
            os_ << "  " << layer_name(layer) << ": { style.stroke: \"" << style::layer_color(layer)
                << "\"; style.font-size: 10 }\n";
        os_ << "  required: { style.stroke-width: 3 }\n";
        os_ << "  recommended: { style.stroke-dash: 6 }\n";
        os_ << "  regex: { style.stroke-dash: 2 }\n";
        os_ << "  dynamic: { style.stroke-dash: 4 }\n";
        os_ << "  unmet: { style.stroke: red; style.stroke-width: 3 }\n";
        os_ << "}\n";
    }

    void label(const std::string& indent, const std::string& key, const std::string& name)
    {
        if (key != name)
            os_ << indent << "label: " << d2_quoted(name) << "\n";
    }

    void write_containers()
    {
        for (const auto& platform : deployment_.platforms)
        {
            if (!filter_.is_node_included(platform.name))
                continue;
            auto platform_id = id("", platform.name);
            os_ << platform_id << ": {\n  class: platform\n";
            label("  ", platform_id,
                  platform.has_modules()
                      ? platform.name
                      : view_.container_label(platform.name, platform.modules.front()));

            for (const auto& module : platform.modules)
            {
                std::string indent = "  ";
                std::string parent = platform_id;
                if (platform.has_modules())
                {
                    if (!filter_.is_node_included(module.name))
                        continue;
                    auto module_id = id(platform_id, module.name);
                    os_ << indent << module_id << ": {\n" << indent << "  class: module\n";
                    label(indent + "  ", module_id, view_.container_label(module.name, module));
                    parent += "." + module_id;
                    indent += "  ";
                }

                for (const auto& app_p : module.applications)
                {
                    const auto& app = app_p.second;
                    if (!filter_.is_node_included(app.name) || view_.folded(app))
                        continue;
                    auto app_id = id(parent, app.name);
                    auto app_path = parent + "." + app_id;
                    os_ << indent << app_id << ": {\n" << indent << "  class: application\n";
                    label(indent + "  ", app_id, app.name);
                    if (view_.collapsed(app))
                    {
                        auto tooltip = pubs_tooltip(app_path);
                        if (!tooltip.empty())
                            os_ << indent << "  tooltip: " << d2_tooltip(tooltip) << "\n";
                    }

                    // drawn threads (a base class is drawn as its most derived class), and any
                    // thread an edge names
                    std::map<std::string, bool> threads;
                    for (const auto& thread_p : app.threads)
                    {
                        const auto& thread = *thread_p.second;
                        if (!view_.collapsed(app) && view_.thread_drawn(app, thread))
                            threads[thread.most_derived_name()] = thread.known;
                    }
                    for (const auto& thread : threads_[app_path]) threads.emplace(thread, true);

                    for (const auto& thread_p : threads)
                        write_thread(indent + "  ", app_path, thread_p.first, thread_p.second);
                    for (const auto& placeholder : placeholders_[app_path])
                        os_ << indent << "  " << placeholder.first
                            << ": { class: placeholder; label: " << d2_quoted(placeholder.second)
                            << " }\n";
                    os_ << indent << "}\n";
                }

                if (platform.has_modules())
                    os_ << "  }\n";
            }
            os_ << "}\n";
        }
    }

    void write_thread(const std::string& indent, const std::string& app_path,
                      const std::string& thread, bool known)
    {
        auto thread_id = id(app_path, thread);
        auto display = display_thread_name(thread);
        os_ << indent << thread_id << ": { class: thread";
        if (thread_id != display)
            os_ << "; label: " << d2_quoted(display);
        if (!known)
            os_ << "; style.stroke-dash: 3";

        auto tooltip = pubs_tooltip(app_path + "." + thread_id);
        if (display != thread)
            tooltip = thread + (tooltip.empty() ? "" : "\n") + tooltip;
        if (!tooltip.empty())
            os_ << "; tooltip: " << d2_tooltip(tooltip);
        os_ << " }\n";
    }

    // the publications drawn from a shape, one per line
    std::string pubs_tooltip(const std::string& path)
    {
        std::string tooltip;
        for (const auto& p : pubs_by_thread_[path])
        {
            const auto& pub = std::get<2>(p);
            tooltip += (tooltip.empty() ? "" : "\n") + pub.publish_index_str() + ": " +
                       pub.display_group() + " | " + pub.scheme + " | " + pub.type;
        }
        return tooltip;
    }

    void write_edges()
    {
        for (const auto& e : edges_)
        {
            auto pub_label = e.pub.publish_index_str();
            os_ << e.from << " -> " << e.to << ": "
                << d2_quoted(pub_label + " [" + short_group(e.pub.display_group()) + "]")
                << " { class: ";
            if (e.classes.size() == 1)
                os_ << e.classes.front();
            else
            {
                os_ << "[";
                for (std::size_t i = 0; i < e.classes.size(); ++i)
                    os_ << (i ? "; " : "") << e.classes[i];
                os_ << "]";
            }
            os_ << "; tooltip: "
                << d2_tooltip(pub_label + ": " + e.pub.display_group() + " | " + e.pub.scheme +
                              " | " + e.pub.type)
                << " }\n";
        }
    }

  private:
    std::ostream& os_;
    const Deployment& deployment_;
    const Resolution& resolution_;
    const D2Params& params_;
    ViewFilter filter_;
    DiagramView view_;
    PublishIndexer indexer_;

    std::map<std::string, std::string> ids_;
    std::map<std::string, std::set<std::string>> used_;
    std::vector<Edge> edges_;
    // application path -> threads named by edges
    std::map<std::string, std::set<std::string>> threads_;
    // application path -> placeholder key -> label
    std::map<std::string, std::map<std::string, std::string>> placeholders_;
    // thread path -> (layer, index, publication), for the thread's tooltip
    std::map<std::string, std::set<std::tuple<Layer, int, PubSubEntry>>> pubs_by_thread_;
};
} // namespace detail

/// \brief Write a D2 diagram of the deployment's publish/subscribe connections
inline void write_d2(std::ostream& os, const Deployment& deployment, const Resolution& resolution,
                     const D2Params& params)
{
    detail::D2Writer(os, deployment, resolution, params).write();
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
