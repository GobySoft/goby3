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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_PUBSUB_ENTRY_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_PUBSUB_ENTRY_H

#include <ostream>
#include <string>

#include <yaml-cpp/yaml.h>

#include "goby/middleware/tool/interface/yaml_raii.h"
#include "goby/middleware/transport/interface.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
enum class Layer
{
    UNKNOWN = -1,
    INTERTHREAD = 0,
    INTERPROCESS = 10,
    INTERMODULE = 20,
    INTERVEHICLE = 30
};

inline std::string layer_name(Layer layer)
{
    switch (layer)
    {
        case Layer::INTERTHREAD: return "interthread";
        case Layer::INTERPROCESS: return "interprocess";
        case Layer::INTERMODULE: return "intermodule";
        case Layer::INTERVEHICLE: return "intervehicle";
        case Layer::UNKNOWN: break;
    }
    return "unknown";
}

inline char layer_code(Layer layer)
{
    switch (layer)
    {
        case Layer::INTERTHREAD: return 'T';
        case Layer::INTERPROCESS: return 'P';
        case Layer::INTERMODULE: return 'M';
        case Layer::INTERVEHICLE: return 'V';
        case Layer::UNKNOWN: break;
    }
    return 'X';
}

inline std::string necessity_name(goby::middleware::Necessity n)
{
    switch (n)
    {
        case goby::middleware::Necessity::REQUIRED: return "required";
        case goby::middleware::Necessity::RECOMMENDED: return "recommended";
        case goby::middleware::Necessity::OPTIONAL: break;
    }
    return "optional";
}

inline goby::middleware::Necessity necessity_from_name(const std::string& s)
{
    if (s == "required")
        return goby::middleware::Necessity::REQUIRED;
    else if (s == "recommended")
        return goby::middleware::Necessity::RECOMMENDED;
    else
        return goby::middleware::Necessity::OPTIONAL;
}

/// \brief One publication or subscription as written to (and read from) an interface file
struct PubSubEntry
{
    enum class Direction
    {
        UNKNOWN,
        PUBLISH,
        SUBSCRIBE
    };

    PubSubEntry() = default;

    PubSubEntry(Layer l, Direction dir, const YAML::Node& yaml, const std::string& th = "")
        : layer(l), direction(dir)
    {
        auto thread_node = yaml["thread"];
        thread = thread_node ? thread_node.as<std::string>() : th;

        group = yaml["group"].as<std::string>();
        scheme = yaml["scheme"].as<std::string>();
        type = yaml["type"].as<std::string>();

        if (dir == Direction::SUBSCRIBE && yaml["necessity"])
            necessity = necessity_from_name(yaml["necessity"].as<std::string>());

        auto inner_node = yaml["inner"];
        is_inner_pub = inner_node && inner_node.as<bool>();

        auto is_regex_node = yaml["is_regex"];
        is_regex = is_regex_node && is_regex_node.as<bool>();
    }

    PubSubEntry(Layer l, Direction d, std::string th, std::string g, std::string s, std::string t,
                bool tk, goby::middleware::Necessity n, bool r)
        : layer(l),
          direction(d),
          thread(std::move(th)),
          group(std::move(g)),
          scheme(std::move(s)),
          type(std::move(t)),
          thread_is_known(tk),
          necessity(n),
          is_regex(r)
    {
    }

    Layer layer{Layer::UNKNOWN};
    Direction direction{Direction::UNKNOWN};

    std::string thread;
    std::string group;
    std::string scheme;
    std::string type;
    bool thread_is_known{true};
    goby::middleware::Necessity necessity{goby::middleware::Necessity::OPTIONAL};
    bool is_regex{false};
    // publication was added to this layer from an outer layer's publisher
    bool is_inner_pub{false};
    // assigned when a deployment is loaded and used only to label diagrams; not part of the
    // ordering, so it may be set on an entry already held in a std::set
    mutable int publish_index{-1};

    std::string publish_index_str() const
    {
        return std::string(1, layer_code(layer)) + std::to_string(publish_index);
    }

    void write_yaml_map(YAML::Emitter& yaml_out, bool include_thread = true, bool inner_pub = false,
                        bool include_necessity = true) const
    {
        goby::yaml::YMap entry_map(yaml_out, false);
        entry_map.add("group", group);
        entry_map.add("scheme", scheme);
        entry_map.add("type", type);
        if (include_necessity)
            entry_map.add("necessity", necessity_name(necessity));
        if (include_thread)
            entry_map.add("thread", thread);
        if (inner_pub)
            entry_map.add("inner", "true");
        if (is_regex)
            entry_map.add("is_regex", "true");
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

/// \brief Would a publication \c a reach subscription \c b (or vice versa) if they share a scope?
inline bool connects(const PubSubEntry& a, const PubSubEntry& b)
{
    if (a.layer != b.layer || a.group != b.group)
        return false;

    // a type regex is given at run time, so a regex subscription takes any type in its group
    bool types_match = a.type == b.type || a.is_regex || b.is_regex;
    if ((a.scheme == b.scheme || a.scheme == "CXX_OBJECT" || b.scheme == "CXX_OBJECT") &&
        types_match)
        return true;

    // mavlink_message_t can be decoded as any MAVLink type
    if (a.scheme == "MAVLINK" && b.scheme == "MAVLINK" &&
        (a.type == "mavlink::__mavlink_message" || b.type == "mavlink::__mavlink_message"))
        return true;

    return false;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
