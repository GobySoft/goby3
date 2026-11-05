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

#include <cstdint>
#include <ostream>
#include <set>
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

        auto dynamic_node = yaml["dynamic"];
        dynamic = dynamic_node && dynamic_node.as<bool>();
        auto numeric_node = yaml["numeric"];
        numeric_runtime = numeric_node && numeric_node.as<std::string>() == "runtime";
        if (auto expr_node = yaml["expr"])
            expr = expr_node.as<std::string>();
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

    // made with publish_dynamic()/subscribe_dynamic() on a DynamicGroup
    bool dynamic{false};
    // the group's string is known but its numeric part is set at run time
    bool numeric_runtime{false};
    // the C++ expression for a group goby_clang_tool could not resolve (group is "*")
    std::string expr;
    // numeric values given to a numeric_runtime group by a deployment's bind:
    std::set<std::uint32_t> bound_numerics;
    // necessity was set by the deployment rather than the code
    bool necessity_overridden{false};

    /// \brief A dynamic group whose string goby_clang_tool could not determine
    bool unresolved() const { return dynamic && group == "*"; }

    /// \brief A dynamic group whose numeric part is still unknown
    bool unbound() const { return numeric_runtime && bound_numerics.empty(); }

    /// \brief The group as a deployment sees it, with any bound numeric values
    std::string display_group() const;
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
        if (dynamic)
            entry_map.add("dynamic", "true");
        if (numeric_runtime)
            entry_map.add("numeric", "runtime");
        if (!expr.empty())
            entry_map.add("expr", expr);
    }
};

/// \brief e.g. {1, 2, 3, 7} -> "1-3,7"
inline std::string compact_numerics(const std::set<std::uint32_t>& values)
{
    std::string out;
    for (auto it = values.begin(); it != values.end();)
    {
        auto first = *it, last = *it;
        for (++it; it != values.end() && *it == last + 1; ++it) last = *it;
        if (!out.empty())
            out += ",";
        out += std::to_string(first);
        if (last != first)
            out += "-" + std::to_string(last);
    }
    return out;
}

inline std::string PubSubEntry::display_group() const
{
    if (unresolved())
        return expr.empty() ? group : "* (" + expr + ")";
    if (!bound_numerics.empty())
        return group + ";" + compact_numerics(bound_numerics);
    if (numeric_runtime)
        return group + ";?";
    return group;
}

/// \brief How a publication's group reaches a subscription's
enum class GroupMatch
{
    NONE,
    EXACT,
    // a numeric part known only at run time might match (an unbound dynamic group)
    LOOSE
};

namespace detail
{
// a group's string and numeric parts: "name;5" from a static Group, or a dynamic group's numerics
struct GroupKey
{
    std::string name;
    enum class Numeric
    {
        NONE,
        VALUES,
        // a static Group's numeric written as an expression goby_clang_tool did not evaluate
        TEXT,
        RUNTIME
    } numeric{Numeric::NONE};
    std::set<std::uint32_t> values;
    std::string text;
};

inline GroupKey group_key(const PubSubEntry& e)
{
    GroupKey key;
    if (e.numeric_runtime)
    {
        key.name = e.group;
        key.numeric =
            e.bound_numerics.empty() ? GroupKey::Numeric::RUNTIME : GroupKey::Numeric::VALUES;
        key.values = e.bound_numerics;
        return key;
    }

    auto semicolon = e.group.rfind(';');
    if (semicolon == std::string::npos)
    {
        key.name = e.group;
        return key;
    }
    key.name = e.group.substr(0, semicolon);
    auto numeric = e.group.substr(semicolon + 1);
    if (!numeric.empty() && numeric.find_first_not_of("0123456789") == std::string::npos)
    {
        key.numeric = GroupKey::Numeric::VALUES;
        key.values.insert(std::stoul(numeric));
    }
    else
    {
        key.numeric = GroupKey::Numeric::TEXT;
        key.text = numeric;
    }
    return key;
}
} // namespace detail

inline GroupMatch group_match(const PubSubEntry& a, const PubSubEntry& b)
{
    if (a.unresolved() || b.unresolved())
        return GroupMatch::NONE;
    if (!a.numeric_runtime && !b.numeric_runtime)
        return a.group == b.group ? GroupMatch::EXACT : GroupMatch::NONE;

    using Numeric = detail::GroupKey::Numeric;
    auto ka = detail::group_key(a), kb = detail::group_key(b);
    if (ka.name != kb.name)
        return GroupMatch::NONE;
    // a numeric set at run time is part of the group string, so it never reaches a group without one
    if (ka.numeric == Numeric::NONE || kb.numeric == Numeric::NONE)
        return GroupMatch::NONE;
    if (ka.numeric == Numeric::RUNTIME || kb.numeric == Numeric::RUNTIME)
        return GroupMatch::LOOSE;
    if (ka.numeric == Numeric::TEXT || kb.numeric == Numeric::TEXT)
        return ka.numeric == kb.numeric && ka.text == kb.text ? GroupMatch::EXACT
                                                              : GroupMatch::NONE;
    for (auto v : ka.values)
    {
        if (kb.values.count(v))
            return GroupMatch::EXACT;
    }
    return GroupMatch::NONE;
}

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
inline GroupMatch connection(const PubSubEntry& a, const PubSubEntry& b)
{
    if (a.layer != b.layer)
        return GroupMatch::NONE;
    auto match = group_match(a, b);
    if (match == GroupMatch::NONE)
        return match;

    // a type regex is given at run time, so a regex subscription takes any type in its group
    bool types_match = a.type == b.type || a.is_regex || b.is_regex;
    if ((a.scheme == b.scheme || a.scheme == "CXX_OBJECT" || b.scheme == "CXX_OBJECT") &&
        types_match)
        return match;

    // mavlink_message_t can be decoded as any MAVLink type
    if (a.scheme == "MAVLINK" && b.scheme == "MAVLINK" &&
        (a.type == "mavlink::__mavlink_message" || b.type == "mavlink::__mavlink_message"))
        return match;

    return GroupMatch::NONE;
}

inline bool connects(const PubSubEntry& a, const PubSubEntry& b)
{
    return connection(a, b) != GroupMatch::NONE;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
