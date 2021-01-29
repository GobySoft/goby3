// Copyright 2020-2021:
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

#ifndef GOBY_APPS_MIDDLEWARE_CLANG_TOOL_PUBSUB_ENTRY_H
#define GOBY_APPS_MIDDLEWARE_CLANG_TOOL_PUBSUB_ENTRY_H

#include <boost/algorithm/string.hpp>
#include <string>

#include "goby/middleware/transport/interface.h"

#include "yaml_raii.h"

namespace viz
{
struct Thread;
}

namespace goby
{
namespace clang
{
enum class Layer
{
    UNKNOWN = -1,
    INTERTHREAD = 0,
    INTERPROCESS = 10,
    INTERMODULE = 20,
    INTERVEHICLE = 30
};

struct PubSubEntry
{
    enum class Direction
    {
        UNKNOWN,
        PUBLISH,
        SUBSCRIBE
    };

    PubSubEntry(Layer l, Direction dir, const YAML::Node& yaml,
                const std::map<std::string, std::shared_ptr<viz::Thread>> threads);

    PubSubEntry(Layer l, Direction dir, const YAML::Node& yaml, const std::string& th = "")
    {
        layer = l;
        direction = dir;
        auto thread_node = yaml["thread"];
        if (thread_node)
            thread = thread_node.as<std::string>();
        else
            thread = th;

        group = yaml["group"].as<std::string>();
        scheme = yaml["scheme"].as<std::string>();
        type = yaml["type"].as<std::string>();

        if (dir == Direction::SUBSCRIBE && yaml["necessity"])
            necessity = as_necessity_enum(yaml["necessity"].as<std::string>());

        auto inner_node = yaml["inner"];
        if (inner_node && inner_node.as<bool>())
            is_inner_pub = true;

        init();
    }

    PubSubEntry(Layer l, Direction d, std::string th, std::string g, std::string s, std::string t,
                bool tk, goby::middleware::Necessity n)
        : layer(l),
          direction(d),
          thread(th),
          group(g),
          scheme(s),
          type(t),
          thread_is_known(tk),
          necessity(n)
    {
        init();
    }

    void init()
    {
        if (direction == Direction::PUBLISH)
        {
            switch (layer)
            {
                case Layer::UNKNOWN: publish_index = get_next_index<Layer::UNKNOWN>(); break;
                case Layer::INTERTHREAD:
                    publish_index = get_next_index<Layer::INTERTHREAD>();
                    break;
                case Layer::INTERMODULE:
                    publish_index = get_next_index<Layer::INTERMODULE>();
                    break;
                case Layer::INTERPROCESS:
                    publish_index = get_next_index<Layer::INTERPROCESS>();
                    break;
                case Layer::INTERVEHICLE:
                    publish_index = get_next_index<Layer::INTERVEHICLE>();
                    break;
            }
        }
    }

    Layer layer{Layer::UNKNOWN};
    Direction direction{Direction::UNKNOWN};

    std::string thread;
    std::string group;
    std::string scheme;
    std::string type;
    bool thread_is_known{true};
    goby::middleware::Necessity necessity{goby::middleware::Necessity::OPTIONAL};
    bool is_inner_pub{false};
    int publish_index{-1};

    std::string publish_index_str() const
    {
        std::string layer_code;
        switch (layer)
        {
            case Layer::UNKNOWN: layer_code = "X"; break;
            case Layer::INTERTHREAD: layer_code = "T"; break;
            case Layer::INTERMODULE: layer_code = "M"; break;
            case Layer::INTERPROCESS: layer_code = "P"; break;
            case Layer::INTERVEHICLE: layer_code = "V"; break;
        }

        return layer_code + std::to_string(publish_index);
    }

    template <Layer l> int get_next_index()
    {
        static int g_publish_index = 0;
        return g_publish_index++;
    }

    void write_yaml_map(YAML::Emitter& yaml_out, bool include_thread = true, bool inner_pub = false,
                        bool include_necessity = true) const
    {
        goby::yaml::YMap entry_map(yaml_out, false);
        entry_map.add("group", group);
        entry_map.add("scheme", scheme);
        entry_map.add("type", type);
        if (include_necessity) // only for subscribers
            entry_map.add("necessity", as_string(necessity));
        if (include_thread)
            entry_map.add("thread", thread);

        // publication was automatically added to this scope from an outer publisher
        if (inner_pub)
            entry_map.add("inner", "true");
    }

    std::string as_string(goby::middleware::Necessity n) const
    {
        switch (n)
        {
            case goby::middleware::Necessity::REQUIRED: return "required";
            case goby::middleware::Necessity::RECOMMENDED: return "recommended";
            default:
            case goby::middleware::Necessity::OPTIONAL: return "optional";
        }
    }

    goby::middleware::Necessity as_necessity_enum(std::string s)
    {
        if (s == "required")
            return goby::middleware::Necessity::REQUIRED;
        else if (s == "recommended")
            return goby::middleware::Necessity::RECOMMENDED;
        else
            return goby::middleware::Necessity::OPTIONAL;
    }
};

inline std::ostream& operator<<(std::ostream& os, const PubSubEntry& e)
{
    return os << "layer: " << static_cast<int>(e.layer) << ", thread: " << e.thread
              << ", group: " << e.group << ", scheme: " << e.scheme << ", type: " << e.type;
}

inline bool operator<(const PubSubEntry& a, const PubSubEntry& b)
{
    if (a.layer != b.layer)
        return a.layer < b.layer;
    if (a.thread != b.thread)
        return a.thread < b.thread;
    if (a.group != b.group)
        return a.group < b.group;
    if (a.scheme != b.scheme)
        return a.scheme < b.scheme;

    return a.type < b.type;
}

inline bool connects(const PubSubEntry& a, const PubSubEntry& b)
{
    return a.layer == b.layer && a.group == b.group &&
           (a.scheme == b.scheme || a.scheme == "CXX_OBJECT" || b.scheme == "CXX_OBJECT") &&
           a.type == b.type;
}

inline void remove_disconnected(PubSubEntry pub, PubSubEntry sub,
                                std::set<PubSubEntry>& disconnected_pubs,
                                std::set<PubSubEntry>& disconnected_subs)
{
    disconnected_pubs.erase(pub);
    disconnected_subs.erase(sub);

    auto cxx_sub = sub;
    cxx_sub.scheme = "CXX_OBJECT";
    disconnected_subs.erase(cxx_sub);
}

} // namespace clang
} // namespace goby

namespace viz
{
struct Thread
{
    Thread(std::string n, bool k, std::set<std::string> b = std::set<std::string>())
        : name(n), known(k), bases(b)
    {
    }
    Thread(std::string n, bool k, const YAML::Node& y,
           std::set<std::string> b = std::set<std::string>())
        : name(n), known(k), bases(b), yaml(y)
    {
    }

    Thread() : name(""), known(true) {}

    void parse_yaml()
    {
        auto publish_node = yaml["publishes"];
        for (auto p : publish_node)
            interthread_publishes.emplace(goby::clang::Layer::INTERTHREAD,
                                          goby::clang::PubSubEntry::Direction::PUBLISH, p,
                                          most_derived_name());

        auto subscribe_node = yaml["subscribes"];
        for (auto s : subscribe_node)
            interthread_subscribes.emplace(goby::clang::Layer::INTERTHREAD,
                                           goby::clang::PubSubEntry::Direction::SUBSCRIBE, s,
                                           most_derived_name());
    }

    std::string most_derived_name()
    {
        if (parent)
            return parent->most_derived_name();
        else
            return name;
    }

    std::string name;
    bool known;
    std::set<std::string> bases;
    YAML::Node yaml;

    // child thread instance if we're not a direct base of SimpleThread
    std::shared_ptr<Thread> child;
    // this thread has a parent who isn't a direct base of SimpleThread
    std::shared_ptr<Thread> parent;

    std::set<goby::clang::PubSubEntry> interthread_publishes;
    std::set<goby::clang::PubSubEntry> interthread_subscribes;
};

inline void html_escape(std::string& s)
{
    using boost::algorithm::replace_all;
    replace_all(s, "&", "&amp;");
    replace_all(s, "\"", "&quot;");
    replace_all(s, "\'", "&apos;");
    replace_all(s, "<", "&lt;");
    replace_all(s, ">", "&gt;");

    boost::algorithm::replace_first(s, "&lt;", "<br/>&lt;");
    replace_all(s, "&lt;", "<font point-size=\"10\">&lt;");
    replace_all(s, "&gt;", "&gt;</font>");

    replace_all(s, ", ", ",<br/>");
}

} // namespace viz

inline goby::clang::PubSubEntry::PubSubEntry(
    Layer l, Direction dir, const YAML::Node& yaml,
    const std::map<std::string, std::shared_ptr<viz::Thread>> threads)
    : PubSubEntry(l, dir, yaml)
{
    if (threads.count(thread))
        thread = threads.at(thread)->most_derived_name();
}

#endif
