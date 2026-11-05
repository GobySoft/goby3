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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_MODEL_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_MODEL_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "goby/middleware/group.h"
#include "goby/middleware/tool/interface/pubsub_entry.h"

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
/// \brief A problem found while loading or checking a deployment
struct Issue
{
    enum class Severity
    {
        ERROR,
        WARNING,
        INFO
    };

    /// \brief The publication or subscription an issue is about, when there is one
    struct Subject
    {
        std::string platform;
        std::string module;
        std::string application;
        std::string thread;
        std::string layer;
        std::string group;
        std::string scheme;
        std::string type;
        std::string necessity;
    };

    Severity severity{Severity::ERROR};
    std::string code;
    std::string message;
    // e.g. "platforms[0].run[2]"
    std::string location;
    Subject subject;
};

using Issues = std::vector<Issue>;

inline void add_issue(Issues& issues, Issue::Severity severity, std::string code,
                      std::string message, std::string location = "")
{
    issues.push_back({severity, std::move(code), std::move(message), std::move(location), {}});
}

inline std::ostream& operator<<(std::ostream& os, const Issue& issue)
{
    switch (issue.severity)
    {
        case Issue::Severity::ERROR: os << "error"; break;
        case Issue::Severity::WARNING: os << "warning"; break;
        case Issue::Severity::INFO: os << "info"; break;
    }
    os << " [" << issue.code << "]";
    if (!issue.location.empty())
        os << " " << issue.location;
    return os << ": " << issue.message;
}

inline bool has_errors(const Issues& issues)
{
    return std::any_of(issues.begin(), issues.end(),
                       [](const Issue& i) { return i.severity == Issue::Severity::ERROR; });
}

/// \brief Hands out the per-layer publication numbers (P0, P1, ...) shown in diagrams
class PublishIndexer
{
  public:
    int next(Layer layer) { return next_[layer]++; }

  private:
    std::map<Layer, int> next_;
};

struct Thread
{
    Thread(std::string n, bool k, std::set<std::string> b = std::set<std::string>())
        : name(std::move(n)), known(k), bases(std::move(b))
    {
    }

    Thread() : known(true) {}

    std::string most_derived_name() const { return parent ? parent->most_derived_name() : name; }

    std::string name;
    bool known;
    std::set<std::string> bases;

    // set when a class derived from this one (rather than SimpleThread directly) is the thread
    // that actually runs
    const Thread* parent{nullptr};

    std::set<PubSubEntry> interthread_publishes;
    std::set<PubSubEntry> interthread_subscribes;
};

struct Application
{
    Application(std::string n) : name(std::move(n)) {}

    std::string name;
    std::map<std::string, std::shared_ptr<Thread>> threads;
    // interprocess, intermodule and intervehicle (interthread entries live in each Thread)
    std::map<Layer, std::set<PubSubEntry>> publishes;
    std::map<Layer, std::set<PubSubEntry>> subscribes;

    const std::set<PubSubEntry>& publishes_on(Layer layer) const
    {
        static const std::set<PubSubEntry> empty;
        auto it = publishes.find(layer);
        return it == publishes.end() ? empty : it->second;
    }

    const std::set<PubSubEntry>& subscribes_on(Layer layer) const
    {
        static const std::set<PubSubEntry> empty;
        auto it = subscribes.find(layer);
        return it == subscribes.end() ? empty : it->second;
    }

    /// \brief Add the contents of one interface file (a root node with application: and the layers)
    /// \brief Turns one entry from an interface file into the entries a deployment sees (a
    /// dynamic group with its bound numerics, or the publications a bind says it stands for)
    using Binder = std::function<std::vector<PubSubEntry>(PubSubEntry)>;

    void merge(const YAML::Node& yaml, PublishIndexer& indexer, const Binder& binder = Binder());

  private:
    std::string most_derived(const std::string& thread) const
    {
        auto it = threads.find(thread);
        return it == threads.end() ? thread : it->second->most_derived_name();
    }

    void insert(std::set<PubSubEntry>& entries, PubSubEntry entry, PublishIndexer& indexer,
                const Binder& binder)
    {
        for (auto& bound : binder ? binder(entry) : std::vector<PubSubEntry>{entry})
        {
            auto result = entries.insert(bound);
            if (result.second && bound.direction == PubSubEntry::Direction::PUBLISH)
                result.first->publish_index = indexer.next(bound.layer);
        }
    }
};

inline void Application::merge(const YAML::Node& yaml, PublishIndexer& indexer,
                               const Binder& binder)
{
    using Direction = PubSubEntry::Direction;

    std::vector<std::pair<std::shared_ptr<Thread>, YAML::Node>> new_thread_yaml;
    if (auto interthread_node = yaml["interthread"])
    {
        for (const auto& thread_node : interthread_node["threads"])
        {
            auto thread_name = thread_node["name"].as<std::string>();
            bool known = !thread_node["known"] || thread_node["known"].as<bool>();
            std::set<std::string> bases;
            for (const auto& base : thread_node["bases"]) bases.insert(base.as<std::string>());

            auto it = threads.find(thread_name);
            if (it == threads.end())
                it = threads
                         .emplace(thread_name, std::make_shared<Thread>(thread_name, known, bases))
                         .first;
            else
                it->second->bases.insert(bases.begin(), bases.end());
            new_thread_yaml.emplace_back(it->second, thread_node);
        }

        // a thread that isn't a direct subclass of SimpleThread is drawn as its most derived class
        for (auto& thread_p : threads)
        {
            auto& thread = *thread_p.second;
            bool is_direct_thread_subclass =
                std::any_of(thread.bases.begin(), thread.bases.end(), [](const std::string& b)
                            { return b.find("goby::middleware::SimpleThread") == 0; });
            if (is_direct_thread_subclass)
                continue;
            for (const auto& base : thread.bases)
            {
                auto base_it = threads.find(base);
                if (base_it != threads.end())
                    base_it->second->parent = &thread;
            }
        }

        std::sort(new_thread_yaml.begin(), new_thread_yaml.end(),
                  [](const auto& a, const auto& b) { return a.first->name < b.first->name; });
        for (auto& thread_yaml : new_thread_yaml)
        {
            auto& thread = *thread_yaml.first;
            for (const auto& p : thread_yaml.second["publishes"])
                insert(thread.interthread_publishes,
                       PubSubEntry(Layer::INTERTHREAD, Direction::PUBLISH, p,
                                   thread.most_derived_name()),
                       indexer, binder);
            for (const auto& s : thread_yaml.second["subscribes"])
                insert(thread.interthread_subscribes,
                       PubSubEntry(Layer::INTERTHREAD, Direction::SUBSCRIBE, s,
                                   thread.most_derived_name()),
                       indexer, binder);
        }
    }

    for (auto layer : {Layer::INTERPROCESS, Layer::INTERMODULE, Layer::INTERVEHICLE})
    {
        auto layer_node = yaml[layer_name(layer)];
        if (!layer_node)
            continue;

        for (const auto& p : layer_node["publishes"])
        {
            PubSubEntry entry(layer, Direction::PUBLISH, p);
            entry.thread = most_derived(entry.thread);
            insert(publishes[layer], entry, indexer, binder);
        }
        for (const auto& s : layer_node["subscribes"])
        {
            PubSubEntry entry(layer, Direction::SUBSCRIBE, s);
            entry.thread = most_derived(entry.thread);
            insert(subscribes[layer], entry, indexer, binder);
        }
    }

    // the main thread of applications without interthread publish/subscribe
    for (const auto* layer_map : {&publishes, &subscribes})
    {
        for (const auto& layer_p : *layer_map)
        {
            for (const auto& e : layer_p.second)
            {
                if (!threads.count(e.thread))
                    threads.emplace(e.thread,
                                    std::make_shared<Thread>(e.thread, e.thread_is_known));
            }
        }
    }
}

struct Module
{
    std::string name;
    std::map<std::string, Application> applications;
    // platform listed its interfaces directly, without modules:
    bool not_a_module{false};
};

using KeyValues = std::vector<std::pair<std::string, std::string>>;

/// \brief systemd: settings, on a run: entry or as a platform's defaults
struct SystemdSettings
{
    // a project unit file, with ${var}s, in place of the built-in unit
    std::optional<std::string> unit_template;
    std::optional<std::string> user;
    std::optional<std::string> group;
    std::optional<std::string> wanted_by;
    std::optional<std::string> description;
    // a systemd Condition...= line, checked when the unit starts
    std::optional<std::string> condition;
    // appended to the [Unit], [Service] and [Install] sections of the built-in unit
    std::optional<std::string> unit;
    std::optional<std::string> service;
    std::optional<std::string> install;
    std::map<std::string, std::string> vars;
    bool skip{false};
};

/// \brief How one run: entry is started by goby_launch
struct LaunchEntry
{
    std::string module;
    // goby_launch's name for the line, and ${app} in its config
    std::string name;
    // empty for a process (a non-Goby program, with no interface file)
    std::string application;
    bool process{false};
    std::optional<std::string> exec;
    std::optional<std::string> config;
    std::optional<std::string> args;
    std::optional<std::string> kill;
    // a bash conditional ([[ ... ]]) deciding at launch whether the entry runs
    std::optional<std::string> when;
    KeyValues env;
    bool enabled{true};
    // gobyd's required_client list; "auto" stands for the derived list
    std::optional<std::vector<std::string>> hold;
    // goby_coroner's expected_name list; "auto" stands for the derived list
    std::optional<std::vector<std::string>> expected;
    // expected: false, leaving this entry out of an automatic expected list
    bool not_expected{false};
    SystemdSettings systemd;
    std::string location;
};

/// \brief A platform's launch: settings
struct LaunchSettings
{
    std::string flags;
    // defaults for Goby applications' entries
    std::optional<std::string> config;
    std::optional<std::string> args;
    std::optional<std::string> kill;
    KeyValues env;
};

/// \brief A platform's systemd: settings
struct PlatformSystemd
{
    // an existing umbrella unit to use instead of a generated <deployment>-<platform>.target
    std::optional<std::string> target;
    // start each unit after the one before it in run:, rather than all at once after gobyd
    bool ordered{false};
    SystemdSettings defaults;
};

struct Platform
{
    std::string name;
    // the name in the deployment file; differs from name for one of a platform's instances
    std::string base_name;
    std::vector<Module> modules;
    // every run: entry, in order, including processes
    std::vector<LaunchEntry> launch;
    LaunchSettings launch_settings;
    PlatformSystemd systemd;

    bool has_modules() const { return !(modules.size() == 1 && modules.front().not_a_module); }
};

struct Deployment
{
    std::string name;
    // the deployment file, which relative paths in it (such as unit templates) are relative to
    std::string file;
    std::vector<Platform> platforms;
    // continues numbering after the loaded publications, for placeholders added when drawing
    PublishIndexer indexer;
};

/// \brief Where to find interface files and the values of ${NAME} in a deployment file
struct LoadParams
{
    // searched, in order, after the deployment file's own directory
    std::vector<std::string> interface_dirs;
    // goby's own installed interfaces (share/goby/interfaces), searched after interface_dirs
    std::string goby_interfaces_dir;
    // ${NAME} values; these override the environment
    std::map<std::string, std::string> defines;
    // load each of a platform's instances: as <name><instance>; otherwise a platform with
    // instances: is loaded once, its binds taking the numerics of every instance
    bool expand_instances{true};
};

namespace detail
{
/// \brief One bind: entry of a platform or of an application in its run: list
struct Bind
{
    std::string location;
    int uses{0};

    // numeric: { group: <name or glob>, numeric: <values>, layer: <optional> }
    std::regex group_pattern;
    std::vector<std::string> numerics;
    bool has_layer{false};
    Layer layer{Layer::UNKNOWN};

    // publication: { dynamic: { layer, scheme, type }, publishes: [...], subscribes: [...] }
    bool is_publication{false};
    std::string select_scheme;
    std::string select_type;
    struct Concrete
    {
        std::string group;
        std::string scheme;
        std::string type;
    };
    std::vector<Concrete> publishes;
    std::vector<Concrete> subscribes;
};

// one entry of a platform's or module's run: list (or of the older interfaces: list)
struct RunRef
{
    // look the interface file up by the application it declares
    std::string application;
    // or read this file
    std::string file;
    // name the application instance (and its node) differently from the application
    std::string name;
    std::vector<std::string> plugins;
    std::string location;
    // an interfaces: entry that names its application, which merges it into that application
    bool merge_intended{false};
    std::vector<std::shared_ptr<Bind>> binds;
    LaunchEntry launch;
};

struct ModuleRef
{
    std::string name;
    std::vector<RunRef> run;
    bool not_a_module{false};
};

struct PlatformRef
{
    std::string location;
    std::vector<ModuleRef> modules;
    std::vector<std::shared_ptr<Bind>> binds;
    std::vector<std::uint32_t> instances;
    LaunchSettings launch_settings;
    PlatformSystemd systemd;
};

/// \brief Replace ${NAME} and ${NAME:-default} from \c defines, then the environment
///
/// "$${" is a literal "${". A name with no value is left as written.
inline std::string expand_variables(const std::string& text,
                                    const std::map<std::string, std::string>& defines,
                                    std::set<std::string>& unset)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text.compare(i, 3, "$${") == 0)
        {
            out += "${";
            i += 2;
            continue;
        }
        if (text.compare(i, 2, "${") != 0)
        {
            out += text[i];
            continue;
        }
        auto close = text.find('}', i + 2);
        if (close == std::string::npos)
        {
            out += text.substr(i);
            break;
        }
        std::string expr = text.substr(i + 2, close - i - 2);
        std::string name = expr, fallback;
        bool has_fallback = false;
        auto sep = expr.find(":-");
        if (sep != std::string::npos)
        {
            name = expr.substr(0, sep);
            fallback = expr.substr(sep + 2);
            has_fallback = true;
        }

        if (name == "instance" || name == "app")
        {
            out += text.substr(i, close - i + 1);
            i = close;
            continue;
        }

        auto define_it = defines.find(name);
        const char* env = std::getenv(name.c_str());
        if (define_it != defines.end())
            out += define_it->second;
        else if (env)
            out += env;
        else if (has_fallback)
            out += fallback;
        else
        {
            unset.insert(name);
            out += text.substr(i, close - i + 1);
        }
        i = close;
    }
    return out;
}

inline bool load_yaml_text(const std::string& text, const std::string& path,
                           const std::string& code, const std::string& location, Issues& issues,
                           YAML::Node& yaml)
{
    try
    {
        yaml = YAML::Load(text);
        return true;
    }
    catch (const std::exception& e)
    {
        add_issue(issues, Issue::Severity::ERROR, code + "-invalid",
                  "cannot parse " + path + ": " + e.what(), location);
    }
    return false;
}

inline bool load_yaml(const std::string& path, const std::string& code, const std::string& location,
                      Issues& issues, YAML::Node& yaml)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        add_issue(issues, Issue::Severity::ERROR, code + "-missing", "cannot open " + path,
                  location);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return load_yaml_text(ss.str(), path, code, location, issues, yaml);
}

inline std::vector<std::string> read_list(const YAML::Node& node)
{
    std::vector<std::string> values;
    if (node && node.IsSequence())
        for (const auto& v : node) values.push_back(v.as<std::string>());
    else if (node && node.IsScalar())
        values.push_back(node.as<std::string>());
    return values;
}

inline bool layer_from_name(const std::string& name, Layer& layer)
{
    for (auto l :
         {Layer::INTERTHREAD, Layer::INTERPROCESS, Layer::INTERMODULE, Layer::INTERVEHICLE})
    {
        if (layer_name(l) == name)
        {
            layer = l;
            return true;
        }
    }
    return false;
}

// "*" matches any run of characters; everything else is literal
inline std::regex glob_regex(const std::string& glob)
{
    std::string re;
    for (char c : glob)
    {
        if (c == '*')
            re += ".*";
        else if (std::string("\\^$.|?+()[]{}").find(c) != std::string::npos)
            re += std::string("\\") + c;
        else
            re += c;
    }
    return std::regex(re);
}

// integer arithmetic (+, -, *, parentheses) on non-negative literals
class Arithmetic
{
  public:
    explicit Arithmetic(std::string text) : text_(std::move(text)) {}

    bool evaluate(long long& value)
    {
        pos_ = 0;
        if (!expr(value))
            return false;
        skip_space();
        return pos_ == text_.size();
    }

  private:
    void skip_space()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }
    bool accept(char c)
    {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }
    bool expr(long long& value)
    {
        if (!term(value))
            return false;
        for (;;)
        {
            long long rhs;
            if (accept('+'))
            {
                if (!term(rhs))
                    return false;
                value += rhs;
            }
            else if (accept('-'))
            {
                if (!term(rhs))
                    return false;
                value -= rhs;
            }
            else
                return true;
        }
    }
    bool term(long long& value)
    {
        if (!factor(value))
            return false;
        long long rhs;
        while (accept('*'))
        {
            if (!factor(rhs))
                return false;
            value *= rhs;
        }
        return true;
    }
    bool factor(long long& value)
    {
        if (accept('('))
            return expr(value) && accept(')');
        skip_space();
        std::size_t start = pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        if (pos_ == start || pos_ - start > 10)
            return false;
        value = std::stoll(text_.substr(start, pos_ - start));
        return true;
    }

    std::string text_;
    std::size_t pos_{0};
};

/// \brief Evaluate a bind's numeric: "5", "151-153", or arithmetic with ${instance}, once per
/// instance given
inline bool eval_numeric(const std::string& text, const std::vector<std::uint32_t>& instances,
                         std::set<std::uint32_t>& values, std::string& error)
{
    static const std::regex range("\\s*([0-9]+)\\s*-\\s*([0-9]+)\\s*");
    static const std::string instance_var = "${instance}";
    auto in_range = [](long long v) { return v >= 0 && v <= Group::maximum_valid_group; };

    std::smatch m;
    if (std::regex_match(text, m, range))
    {
        auto first = std::stoll(m[1]), last = std::stoll(m[2]);
        if (!in_range(first) || !in_range(last) || last < first || last - first > 100000)
        {
            error = "'" + text + "' is not a valid range of group numbers";
            return false;
        }
        for (auto v = first; v <= last; ++v) values.insert(v);
        return true;
    }

    bool uses_instance = text.find(instance_var) != std::string::npos;
    if (uses_instance && instances.empty())
    {
        error = "'" + text + "' uses ${instance}, but its platform has no instances:";
        return false;
    }
    for (auto instance : uses_instance ? instances : std::vector<std::uint32_t>{0})
    {
        std::string expanded = text;
        for (auto pos = expanded.find(instance_var); pos != std::string::npos;
             pos = expanded.find(instance_var, pos))
            expanded.replace(pos, instance_var.size(), std::to_string(instance));
        long long value;
        if (!Arithmetic(expanded).evaluate(value) || !in_range(value))
        {
            error = "'" + text + "' is not a group number, a range (a-b) or arithmetic on " +
                    "${instance}";
            return false;
        }
        values.insert(value);
    }
    return true;
}

inline std::vector<std::shared_ptr<Bind>> read_binds(const YAML::Node& node,
                                                     const std::string& location, Issues& issues)
{
    std::vector<std::shared_ptr<Bind>> binds;
    if (!node)
        return binds;
    if (!node.IsSequence())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema", "bind: must be a sequence",
                  location);
        return binds;
    }

    int index = 0;
    for (const auto& entry : node)
    {
        auto bind = std::make_shared<Bind>();
        bind->location = location + "[" + std::to_string(index++) + "]";
        auto fail = [&](const std::string& msg)
        { add_issue(issues, Issue::Severity::ERROR, "deployment-schema", msg, bind->location); };

        if (!entry.IsMap())
        {
            fail("a bind must be a map");
            continue;
        }
        static const std::set<std::string> keys{"group",   "numeric",   "layer",
                                                "dynamic", "publishes", "subscribes"};
        bool ok = true;
        for (const auto& kv : entry)
        {
            if (!keys.count(kv.first.as<std::string>()))
            {
                fail("unknown key '" + kv.first.as<std::string>() + "'");
                ok = false;
            }
        }
        if (!ok)
            continue;

        if (entry["dynamic"])
        {
            bind->is_publication = true;
            auto select = entry["dynamic"];
            if (!select.IsMap())
            {
                fail("dynamic: selects a dynamic publication or subscription by layer, scheme "
                     "and type");
                continue;
            }
            if (select["layer"])
            {
                bind->has_layer = layer_from_name(select["layer"].as<std::string>(), bind->layer);
                if (!bind->has_layer)
                {
                    fail("unknown layer '" + select["layer"].as<std::string>() + "'");
                    continue;
                }
            }
            if (select["scheme"])
                bind->select_scheme = select["scheme"].as<std::string>();
            if (select["type"])
                bind->select_type = select["type"].as<std::string>();

            for (auto kind : {"publishes", "subscribes"})
            {
                auto& concrete =
                    std::string(kind) == "publishes" ? bind->publishes : bind->subscribes;
                for (const auto& c : entry[kind])
                {
                    if (!c.IsMap() || !c["group"])
                    {
                        fail(std::string(kind) + " entries need group: (and type:, scheme: if "
                                                 "they differ from the dynamic entry's)");
                        ok = false;
                        continue;
                    }
                    concrete.push_back({c["group"].as<std::string>(),
                                        c["scheme"] ? c["scheme"].as<std::string>() : "",
                                        c["type"] ? c["type"].as<std::string>() : ""});
                }
            }
            if (!ok)
                continue;
            if (bind->publishes.empty() && bind->subscribes.empty())
            {
                fail("a dynamic: bind needs publishes: or subscribes:");
                continue;
            }
        }
        else
        {
            if (!entry["group"] || !entry["numeric"])
            {
                fail("a bind needs group: and numeric:, or dynamic: with publishes: or "
                     "subscribes:");
                continue;
            }
            bind->group_pattern = glob_regex(entry["group"].as<std::string>());
            bind->numerics = read_list(entry["numeric"]);
            if (entry["layer"])
            {
                bind->has_layer = layer_from_name(entry["layer"].as<std::string>(), bind->layer);
                if (!bind->has_layer)
                {
                    fail("unknown layer '" + entry["layer"].as<std::string>() + "'");
                    continue;
                }
            }
            for (const auto& numeric : bind->numerics)
            {
                std::set<std::uint32_t> values;
                std::string error;
                if (!eval_numeric(numeric, {1}, values, error))
                {
                    fail(error);
                    ok = false;
                }
            }
            if (!ok)
                continue;
        }
        binds.push_back(bind);
    }
    return binds;
}

inline Application::Binder make_binder(std::vector<std::shared_ptr<Bind>> binds,
                                       std::vector<std::uint32_t> instances)
{
    return [binds, instances](PubSubEntry e) -> std::vector<PubSubEntry>
    {
        if (!e.dynamic)
            return {e};

        for (const auto& bind : binds)
        {
            if (bind->has_layer && bind->layer != e.layer)
                continue;

            if (!bind->is_publication)
            {
                if (!e.numeric_runtime || !std::regex_match(e.group, bind->group_pattern))
                    continue;
                for (const auto& numeric : bind->numerics)
                {
                    std::string error;
                    eval_numeric(numeric, instances, e.bound_numerics, error);
                }
                ++bind->uses;
                return {e};
            }

            if (!e.unresolved() && !e.numeric_runtime)
                continue;
            if ((!bind->select_scheme.empty() && bind->select_scheme != e.scheme) ||
                (!bind->select_type.empty() && bind->select_type != e.type))
                continue;
            const auto& concrete =
                e.direction == PubSubEntry::Direction::PUBLISH ? bind->publishes : bind->subscribes;
            if (concrete.empty())
                continue;

            ++bind->uses;
            std::vector<PubSubEntry> bound;
            for (const auto& c : concrete)
            {
                PubSubEntry b = e;
                b.group = c.group;
                if (!c.scheme.empty())
                    b.scheme = c.scheme;
                if (!c.type.empty())
                    b.type = c.type;
                b.numeric_runtime = false;
                b.expr.clear();
                bound.push_back(b);
            }
            return bound;
        }
        return {e};
    };
}

inline bool valid_kill(const std::string& kill)
{
    return kill == "goby_terminate" || kill == "SIGTERM" || kill == "SIGINT" || kill == "SIGKILL";
}

inline KeyValues read_env(const YAML::Node& node, const std::string& location, Issues& issues)
{
    KeyValues env;
    if (!node)
        return env;
    if (!node.IsMap())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                  "env: must be a map of NAME: value", location);
        return env;
    }
    for (const auto& kv : node)
    {
        auto value = kv.second.as<std::string>();
        // goby_launch splits its [directives] on spaces, commas and semicolons
        if (value.find_first_of(" ,;") != std::string::npos)
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "env: " + kv.first.as<std::string>() +
                          " contains a space, comma or semicolon, which goby_launch cannot pass",
                      location);
        env.emplace_back(kv.first.as<std::string>(), value);
    }
    return env;
}

inline SystemdSettings read_systemd_settings(const YAML::Node& node, const std::string& location,
                                             Issues& issues);

// the launch keys of a run: entry
inline bool read_launch(const YAML::Node& entry, RunRef& ref, Issues& issues)
{
    auto& launch = ref.launch;
    launch.location = ref.location;
    auto text = [&](const char* key, std::optional<std::string>& value)
    {
        if (entry[key])
            value = entry[key].as<std::string>();
    };

    if (entry["process"])
    {
        if (entry["application"] || entry["file"] || entry["plugins"] || entry["bind"])
        {
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "a process: entry is not a Goby application, so it has no application:, "
                      "file:, plugins: or bind:",
                      ref.location);
            return false;
        }
        launch.process = true;
        launch.name = entry["process"].as<std::string>();
    }
    text("exec", launch.exec);
    text("config", launch.config);
    text("args", launch.args);
    text("kill", launch.kill);
    text("when", launch.when);
    launch.env = read_env(entry["env"], ref.location + ".env", issues);
    launch.systemd = read_systemd_settings(entry["systemd"], ref.location + ".systemd", issues);
    if (entry["enabled"])
        launch.enabled = entry["enabled"].as<bool>();
    if (launch.kill && !valid_kill(*launch.kill))
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                  "kill: must be goby_terminate, SIGTERM, SIGINT or SIGKILL", ref.location);
        return false;
    }
    if (entry["hold"])
        launch.hold = read_list(entry["hold"]);
    if (auto expected = entry["expected"])
    {
        if (expected.IsScalar() && (expected.as<std::string>() == "false"))
            launch.not_expected = true;
        else
            launch.expected = read_list(expected);
    }
    return true;
}

inline LaunchSettings read_launch_settings(const YAML::Node& node, const std::string& location,
                                           Issues& issues)
{
    LaunchSettings settings;
    if (!node)
        return settings;
    if (!node.IsMap())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                  "launch: must be a map (flags:, defaults:)", location);
        return settings;
    }
    for (const auto& kv : node)
    {
        auto key = kv.first.as<std::string>();
        if (key != "flags" && key != "defaults")
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "unknown key '" + key + "'", location);
    }
    if (node["flags"])
        settings.flags = node["flags"].as<std::string>();
    if (auto defaults = node["defaults"])
    {
        for (const auto& kv : defaults)
        {
            auto key = kv.first.as<std::string>();
            if (key == "config")
                settings.config = kv.second.as<std::string>();
            else if (key == "args")
                settings.args = kv.second.as<std::string>();
            else if (key == "kill")
                settings.kill = kv.second.as<std::string>();
            else if (key == "env")
                settings.env = read_env(kv.second, location + ".defaults.env", issues);
            else
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          "unknown key '" + key + "' (defaults: takes config, args, kill, env)",
                          location + ".defaults");
        }
        if (settings.kill && !valid_kill(*settings.kill))
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "kill: must be goby_terminate, SIGTERM, SIGINT or SIGKILL",
                      location + ".defaults");
    }
    return settings;
}

inline std::string with_instance(std::string text, const std::vector<std::uint32_t>& instances)
{
    if (instances.size() != 1)
        return text;
    static const std::string var = "${instance}";
    for (auto pos = text.find(var); pos != std::string::npos; pos = text.find(var, pos))
        text.replace(pos, var.size(), std::to_string(instances.front()));
    return text;
}

inline LaunchSettings for_instance(LaunchSettings settings,
                                   const std::vector<std::uint32_t>& instances)
{
    settings.flags = with_instance(settings.flags, instances);
    for (auto* field : {&settings.config, &settings.args})
    {
        if (*field)
            **field = with_instance(**field, instances);
    }
    for (auto& kv : settings.env) kv.second = with_instance(kv.second, instances);
    return settings;
}

inline SystemdSettings read_systemd_settings(const YAML::Node& node, const std::string& location,
                                             Issues& issues)
{
    SystemdSettings settings;
    if (!node)
        return settings;
    if (!node.IsMap())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema", "systemd: must be a map",
                  location);
        return settings;
    }
    for (const auto& kv : node)
    {
        auto key = kv.first.as<std::string>();
        auto text = [&]() { return kv.second.as<std::string>(); };
        if (key == "template")
            settings.unit_template = text();
        else if (key == "user")
            settings.user = text();
        else if (key == "group")
            settings.group = text();
        else if (key == "wanted_by")
            settings.wanted_by = text();
        else if (key == "description")
            settings.description = text();
        else if (key == "condition")
            settings.condition = text();
        else if (key == "unit")
            settings.unit = text();
        else if (key == "service")
            settings.service = text();
        else if (key == "install")
            settings.install = text();
        else if (key == "skip")
            settings.skip = kv.second.as<bool>();
        else if (key == "vars" && kv.second.IsMap())
        {
            for (const auto& var : kv.second)
                settings.vars[var.first.as<std::string>()] = var.second.as<std::string>();
        }
        else
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "unknown key '" + key + "'", location);
    }
    return settings;
}

inline PlatformSystemd read_platform_systemd(const YAML::Node& node, const std::string& location,
                                             Issues& issues)
{
    PlatformSystemd systemd;
    if (!node)
        return systemd;
    if (!node.IsMap())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                  "systemd: must be a map (target:, ordered:, defaults:)", location);
        return systemd;
    }
    for (const auto& kv : node)
    {
        auto key = kv.first.as<std::string>();
        if (key == "target")
            systemd.target = kv.second.as<std::string>();
        else if (key == "ordered")
            systemd.ordered = kv.second.as<bool>();
        else if (key == "defaults")
            systemd.defaults = read_systemd_settings(kv.second, location + ".defaults", issues);
        else
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "unknown key '" + key + "'", location);
    }
    return systemd;
}

inline SystemdSettings for_instance(SystemdSettings settings,
                                    const std::vector<std::uint32_t>& instances)
{
    for (auto* field : {&settings.unit_template, &settings.user, &settings.group,
                        &settings.wanted_by, &settings.description, &settings.condition,
                        &settings.unit, &settings.service, &settings.install})
    {
        if (*field)
            **field = with_instance(**field, instances);
    }
    for (auto& kv : settings.vars) kv.second = with_instance(kv.second, instances);
    return settings;
}

inline LaunchEntry for_instance(LaunchEntry entry, const std::vector<std::uint32_t>& instances)
{
    entry.systemd = for_instance(entry.systemd, instances);
    for (auto* field : {&entry.exec, &entry.config, &entry.args, &entry.when})
    {
        if (*field)
            **field = with_instance(**field, instances);
    }
    for (auto& kv : entry.env) kv.second = with_instance(kv.second, instances);
    return entry;
}

inline std::vector<RunRef> read_run(const YAML::Node& node, bool legacy,
                                    const std::string& location, Issues& issues)
{
    std::vector<RunRef> refs;
    if (!node.IsSequence())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema", "must be a sequence",
                  location);
        return refs;
    }

    int index = 0;
    for (const auto& entry : node)
    {
        RunRef ref;
        ref.location = location + "[" + std::to_string(index++) + "]";
        ref.launch.location = ref.location;
        if (entry.IsScalar())
        {
            (legacy ? ref.file : ref.application) = entry.as<std::string>();
        }
        else if (entry.IsMap())
        {
            static const std::set<std::string> legacy_keys{"file", "application", "bind"};
            static const std::set<std::string> run_keys{
                "application", "file",   "name",     "plugins", "bind", "process",
                "exec",        "config", "args",     "env",     "kill", "when",
                "enabled",     "hold",   "expected", "systemd"};
            const auto& allowed = legacy ? legacy_keys : run_keys;
            bool bad_key = false;
            for (const auto& kv : entry)
            {
                auto key = kv.first.as<std::string>();
                if (!allowed.count(key))
                {
                    add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                              "unknown key '" + key + "'", ref.location);
                    bad_key = true;
                }
            }
            if (bad_key)
                continue;

            if (entry["file"])
                ref.file = entry["file"].as<std::string>();
            ref.binds = read_binds(entry["bind"], ref.location + ".bind", issues);
            if (legacy)
            {
                if (entry["application"])
                {
                    ref.name = entry["application"].as<std::string>();
                    ref.merge_intended = true;
                }
            }
            else
            {
                if (entry["application"])
                    ref.application = entry["application"].as<std::string>();
                if (entry["name"])
                    ref.name = entry["name"].as<std::string>();
                ref.plugins = read_list(entry["plugins"]);
                if (!read_launch(entry, ref, issues))
                    continue;
            }
            if (ref.file.empty() && ref.application.empty() && !ref.launch.process)
            {
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          legacy ? "an interfaces entry that is a map must have file:"
                                 : "a run entry must name an application:, a file: or a process:",
                          ref.location);
                continue;
            }
        }
        else
        {
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "must be an application name or a map", ref.location);
            continue;
        }
        refs.push_back(ref);
    }
    return refs;
}

/// \brief Reads the application list of a platform or module: run: (by application name) or
/// the older interfaces: (by file)
inline bool read_applications(const YAML::Node& node, const std::string& location,
                              std::vector<RunRef>& refs, Issues& issues)
{
    if (node["run"] && node["interfaces"])
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                  "use run: or interfaces:, not both", location);
        return false;
    }
    if (node["run"])
        refs = read_run(node["run"], false, location + ".run", issues);
    else if (node["interfaces"])
        refs = read_run(node["interfaces"], true, location + ".interfaces", issues);
    else
        return false;
    return true;
}

/// \brief The application an interface file declares, or "" if it is not one (generated
/// interface files start with "application: <name>")
inline std::string declared_application(const std::string& path)
{
    try
    {
        auto yaml = YAML::LoadFile(path);
        if (yaml.IsMap() && yaml["application"] && yaml["application"].IsScalar())
            return yaml["application"].as<std::string>();
    }
    catch (const std::exception&)
    {
    }
    return "";
}

/// \brief Finds interface files by path, or by the application: they declare
class InterfaceFinder
{
  public:
    InterfaceFinder(std::vector<std::string> dirs) : dirs_(std::move(dirs)) {}

    const std::vector<std::string>& dirs() const { return dirs_; }

    std::string find_file(const std::string& file) const
    {
        namespace fs = std::filesystem;
        if (fs::path(file).is_absolute() || fs::exists(file))
            return fs::exists(file) ? file : "";
        for (const auto& dir : dirs_)
        {
            auto candidate = fs::path(dir) / file;
            if (fs::exists(candidate))
                return candidate.string();
        }
        return "";
    }

    std::string find_application(const std::string& application, Issues& issues)
    {
        if (!indexed_)
            index();
        auto it = index_.find(application);
        if (it == index_.end())
            return "";
        const auto& files = it->second;
        if (files.size() > 1 && reported_shadowed_.insert(application).second)
        {
            std::string others;
            for (auto f = files.begin() + 1; f != files.end(); ++f)
                others += (others.empty() ? "" : ", ") + *f;
            add_issue(issues, Issue::Severity::INFO, "interface-shadowed",
                      "application " + application + " is declared by " + files.front() +
                          ", which is used, and by " + others);
        }
        return files.front();
    }

    std::string searched() const
    {
        std::string out;
        for (const auto& dir : dirs_) out += (out.empty() ? "" : ", ") + dir;
        return out;
    }

  private:
    void index()
    {
        namespace fs = std::filesystem;
        indexed_ = true;
        for (const auto& dir : dirs_)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                continue;
            std::vector<fs::path> files;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                auto ext = it->path().extension().string();
                if (ext == ".yml" || ext == ".yaml")
                    files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files)
            {
                std::string application = declared_application(file.string());
                if (application.empty())
                    continue;
                index_[application].push_back(file.string());
            }
        }
    }

    std::vector<std::string> dirs_;
    bool indexed_{false};
    // application -> files declaring it, in search order
    std::map<std::string, std::vector<std::string>> index_;
    std::set<std::string> reported_shadowed_;
};

/// \brief Apply a deployment's necessity: list, which sets the necessity of the subscriptions
/// it selects whatever the code said
inline void apply_necessity_overrides(const YAML::Node& node, Deployment& deployment,
                                      Issues& issues)
{
    if (!node)
        return;
    if (!node.IsSequence())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                  "necessity: must be a sequence", "necessity");
        return;
    }

    static const std::vector<std::string> selectors{"platform", "module", "application", "thread",
                                                    "layer",    "group",  "type"};
    int index = 0;
    for (const auto& entry : node)
    {
        std::string location = "necessity[" + std::to_string(index++) + "]";
        if (!entry.IsMap() || !entry["set"])
        {
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "a necessity override needs set: required|recommended|optional", location);
            continue;
        }
        auto set_name = entry["set"].as<std::string>();
        if (set_name != "required" && set_name != "recommended" && set_name != "optional")
        {
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "set: must be required, recommended or optional", location);
            continue;
        }
        std::map<std::string, std::regex> select;
        bool ok = true;
        for (const auto& kv : entry)
        {
            auto key = kv.first.as<std::string>();
            if (key == "set")
                continue;
            if (std::find(selectors.begin(), selectors.end(), key) == selectors.end())
            {
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          "unknown key '" + key + "'", location);
                ok = false;
                continue;
            }
            try
            {
                select[key] = std::regex(kv.second.as<std::string>());
            }
            catch (const std::regex_error& e)
            {
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          key + ": is not a valid regex: " + e.what(), location);
                ok = false;
            }
        }
        if (!ok)
            continue;

        auto matches = [&](const std::string& key, const std::string& value)
        {
            auto it = select.find(key);
            return it == select.end() || std::regex_match(value, it->second);
        };

        int uses = 0;
        auto apply = [&](std::set<PubSubEntry>& subs, const Platform& platform,
                         const Module& module, const Application& app)
        {
            if (!(matches("platform", platform.name) || matches("platform", platform.base_name)) ||
                !matches("module", module.name) || !matches("application", app.name))
                return;
            std::set<PubSubEntry> updated;
            for (auto e : subs)
            {
                if (matches("thread", e.thread) && matches("layer", layer_name(e.layer)) &&
                    matches("group", e.group) && matches("type", e.type))
                {
                    e.necessity = necessity_from_name(set_name);
                    e.necessity_overridden = true;
                    ++uses;
                }
                updated.insert(e);
            }
            subs = updated;
        };

        for (auto& platform : deployment.platforms)
        {
            for (auto& module : platform.modules)
            {
                for (auto& app_p : module.applications)
                {
                    auto& app = app_p.second;
                    for (auto& thread_p : app.threads)
                        apply(thread_p.second->interthread_subscribes, platform, module, app);
                    for (auto& layer_p : app.subscribes)
                        apply(layer_p.second, platform, module, app);
                }
            }
        }
        if (uses == 0)
            add_issue(issues, Issue::Severity::WARNING, "necessity-unused",
                      "this override matches no subscription", location);
    }
}

inline std::vector<std::string> search_dirs(const std::string& deployment_path,
                                            const LoadParams& params)
{
    namespace fs = std::filesystem;
    std::vector<std::string> dirs;
    auto deployment_dir = fs::path(deployment_path).parent_path().string();
    dirs.push_back(deployment_dir.empty() ? "." : deployment_dir);
    dirs.insert(dirs.end(), params.interface_dirs.begin(), params.interface_dirs.end());
    if (const char* env = std::getenv("GOBY_INTERFACE_PATH"))
    {
        std::stringstream ss(env);
        for (std::string dir; std::getline(ss, dir, ':');)
            if (!dir.empty())
                dirs.push_back(dir);
    }
    if (!params.goby_interfaces_dir.empty())
        dirs.push_back(params.goby_interfaces_dir);
    dirs.push_back(".");

    std::vector<std::string> unique;
    std::set<std::string> seen;
    for (const auto& dir : dirs)
    {
        std::error_code ec;
        auto canonical = fs::weakly_canonical(dir, ec).string();
        if (seen.insert(ec ? dir : canonical).second)
            unique.push_back(dir);
    }
    return unique;
}

} // namespace detail

/// \brief Load a deployment file, or a single interface file as a one-platform stub deployment
///
/// Problems are added to \c issues; on an error the returned deployment may be incomplete.
inline Deployment load_deployment(const std::string& path, Issues& issues,
                                  const LoadParams& params = LoadParams())
{
    Deployment deployment;
    deployment.file = path;

    auto defines = params.defines;
    if (!params.goby_interfaces_dir.empty() && !defines.count("GOBY_INTERFACES_DIR"))
        defines["GOBY_INTERFACES_DIR"] = params.goby_interfaces_dir;

    std::ifstream in(path);
    if (!in.is_open())
    {
        add_issue(issues, Issue::Severity::ERROR, "deployment-missing", "cannot open " + path);
        return deployment;
    }
    std::stringstream raw;
    raw << in.rdbuf();
    std::set<std::string> unset;
    YAML::Node deploy_yaml;
    if (!detail::load_yaml_text(detail::expand_variables(raw.str(), defines, unset), path,
                                "deployment", "", issues, deploy_yaml))
        return deployment;
    for (const auto& name : unset)
        add_issue(issues, Issue::Severity::INFO, "variable-unset",
                  "${" + name +
                      "} has no value here, so it is left for the shell (set it with -D " + name +
                      "=value)");

    detail::InterfaceFinder finder(detail::search_dirs(path, params));
    std::map<std::string, detail::PlatformRef> platform_refs;

    if (!deploy_yaml.IsMap() || !deploy_yaml["deployment"])
    {
        if (deploy_yaml.IsMap() && deploy_yaml["application"])
        {
            deployment.name = deploy_yaml["application"].as<std::string>() + "_stub_deployment";
            detail::RunRef ref;
            ref.file = path;
            ref.location = "application";
            platform_refs["goby_platform"].modules.push_back({"nomodule", {ref}, true});
        }
        else
        {
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "must specify 'deployment: name' in a deployment file, or give a single "
                      "interface file");
            return deployment;
        }
    }
    else
    {
        deployment.name = deploy_yaml["deployment"].as<std::string>();

        auto platforms_node = deploy_yaml["platforms"];
        if (!platforms_node || !platforms_node.IsSequence())
        {
            add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                      "platforms: must be a sequence", "platforms");
            return deployment;
        }

        int platform_index = 0;
        for (const auto& platform : platforms_node)
        {
            std::string location = "platforms[" + std::to_string(platform_index++) + "]";
            if (!platform["name"])
            {
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          "each platform must have name:", location);
                continue;
            }
            auto platform_name = platform["name"].as<std::string>();
            auto& platform_ref = platform_refs[platform_name];
            platform_ref.location = location;
            platform_ref.binds = detail::read_binds(platform["bind"], location + ".bind", issues);
            platform_ref.launch_settings =
                detail::read_launch_settings(platform["launch"], location + ".launch", issues);
            platform_ref.systemd =
                detail::read_platform_systemd(platform["systemd"], location + ".systemd", issues);
            if (auto instances_node = platform["instances"])
            {
                for (const auto& instance : detail::read_list(instances_node))
                {
                    std::set<std::uint32_t> values;
                    std::string error;
                    if (!detail::eval_numeric(instance, {}, values, error))
                        add_issue(issues, Issue::Severity::ERROR, "deployment-schema", error,
                                  location + ".instances");
                    platform_ref.instances.insert(platform_ref.instances.end(), values.begin(),
                                                  values.end());
                }
            }

            if (auto modules_node = platform["modules"])
            {
                int module_index = 0;
                for (const auto& module : modules_node)
                {
                    std::string module_location =
                        location + ".modules[" + std::to_string(module_index++) + "]";
                    detail::ModuleRef module_ref;
                    if (!module["name"] ||
                        !detail::read_applications(module, module_location, module_ref.run, issues))
                    {
                        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                                  "each module must have name: and a run: (or interfaces:) list",
                                  module_location);
                        continue;
                    }
                    module_ref.name = module["name"].as<std::string>();
                    platform_ref.modules.push_back(module_ref);
                }
            }
            else
            {
                detail::ModuleRef module_ref{"nomodule", {}, true};
                if (!detail::read_applications(platform, location, module_ref.run, issues))
                {
                    add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                              "a platform must have modules:, or a run: (or interfaces:) list",
                              location);
                    continue;
                }
                platform_ref.modules = {module_ref};
            }
        }
    }

    auto locate = [&](const detail::RunRef& ref, const std::string& application,
                      const std::string& file) -> std::string
    {
        if (!file.empty())
        {
            auto found = finder.find_file(file);
            if (found.empty())
                add_issue(issues, Issue::Severity::ERROR, "interface-missing",
                          "cannot find " + file + " (searched: " + finder.searched() + ")",
                          ref.location);
            return found;
        }
        auto found = finder.find_application(application, issues);
        if (found.empty())
            add_issue(issues, Issue::Severity::ERROR, "interface-missing",
                      "no interface file declares application " + application +
                          " (searched: " + finder.searched() + ")",
                      ref.location);
        return found;
    };

    auto load_interface = [&](const std::string& file, const detail::RunRef& ref,
                              YAML::Node& yaml) -> bool
    {
        if (!detail::load_yaml(file, "interface", ref.location, issues, yaml))
            return false;
        if (!yaml.IsMap() || !yaml["application"] || !yaml["application"].IsScalar())
        {
            add_issue(issues, Issue::Severity::ERROR, "interface-invalid",
                      file + " has no application:", ref.location);
            return false;
        }
        return true;
    };

    auto merge = [&](Application& app, const YAML::Node& yaml, const std::string& file,
                     const detail::RunRef& ref, const Application::Binder& binder)
    {
        try
        {
            app.merge(yaml, deployment.indexer, binder);
        }
        catch (const std::exception& e)
        {
            add_issue(issues, Issue::Severity::ERROR, "interface-invalid", file + ": " + e.what(),
                      ref.location);
        }
    };

    auto load_platform = [&](const std::string& name, const detail::PlatformRef& platform_ref,
                             const std::vector<std::uint32_t>& instances, Platform& platform)
    {
        platform.name = name;
        platform.launch_settings = detail::for_instance(platform_ref.launch_settings, instances);
        platform.systemd = platform_ref.systemd;
        platform.systemd.defaults = detail::for_instance(platform_ref.systemd.defaults, instances);
        for (const auto& module_ref : platform_ref.modules)
        {
            Module module;
            module.name = module_ref.name;
            module.not_a_module = module_ref.not_a_module;
            for (const auto& ref : module_ref.run)
            {
                auto launch = detail::for_instance(ref.launch, instances);
                launch.module = module.name;
                if (launch.process)
                {
                    platform.launch.push_back(launch);
                    continue;
                }

                auto file = locate(ref, ref.application, ref.file);
                YAML::Node yaml;
                if (file.empty() || !load_interface(file, ref, yaml))
                {
                    launch.application = ref.application;
                    launch.name = ref.name.empty() ? ref.application : ref.name;
                    if (launch.name.empty())
                        launch.name = std::filesystem::path(ref.file).stem().string();
                    platform.launch.push_back(launch);
                    continue;
                }

                std::string app_name =
                    ref.name.empty() ? yaml["application"].as<std::string>() : ref.name;
                launch.application = yaml["application"].as<std::string>();
                launch.name = app_name;
                // a second entry for the same application (a plugin's interface file) is not a
                // second process
                bool already_launched = std::any_of(
                    platform.launch.begin(), platform.launch.end(), [&](const LaunchEntry& e)
                    { return e.module == launch.module && e.name == launch.name; });
                if (!already_launched)
                    platform.launch.push_back(launch);

                auto it = module.applications.find(app_name);
                if (it == module.applications.end())
                {
                    it = module.applications.emplace(app_name, Application(app_name)).first;
                }
                else if (!ref.merge_intended)
                {
                    add_issue(issues, Issue::Severity::WARNING, "application-duplicate",
                              "application " + app_name + " is listed more than once on " +
                                  platform.name + (module.not_a_module ? "" : "/" + module.name) +
                                  "; the entries were merged (give each a distinct name:, or "
                                  "list plugins under plugins:)",
                              ref.location);
                }
                auto binds = ref.binds;
                binds.insert(binds.end(), platform_ref.binds.begin(), platform_ref.binds.end());
                auto binder = detail::make_binder(binds, instances);
                merge(it->second, yaml, file, ref, binder);

                for (const auto& plugin : ref.plugins)
                {
                    bool is_file = std::filesystem::path(plugin).extension() == ".yml" ||
                                   std::filesystem::path(plugin).extension() == ".yaml";
                    auto plugin_file = locate(ref, is_file ? "" : plugin, is_file ? plugin : "");
                    YAML::Node plugin_yaml;
                    if (!plugin_file.empty() && load_interface(plugin_file, ref, plugin_yaml))
                        merge(it->second, plugin_yaml, plugin_file, ref, binder);
                }
            }
            platform.modules.push_back(std::move(module));
        }
        std::stable_sort(platform.modules.begin(), platform.modules.end(),
                         [](const Module& a, const Module& b) { return a.name < b.name; });
    };

    for (const auto& platform_p : platform_refs)
    {
        const auto& ref = platform_p.second;
        if (ref.instances.empty() || !params.expand_instances)
        {
            Platform platform;
            platform.base_name = platform_p.first;
            load_platform(platform_p.first, ref, ref.instances, platform);
            deployment.platforms.push_back(std::move(platform));
        }
        else
        {
            for (auto instance : ref.instances)
            {
                Platform platform;
                platform.base_name = platform_p.first;
                load_platform(platform_p.first + std::to_string(instance), ref, {instance},
                              platform);
                deployment.platforms.push_back(std::move(platform));
            }
        }
    }
    std::stable_sort(deployment.platforms.begin(), deployment.platforms.end(),
                     [](const Platform& a, const Platform& b) { return a.name < b.name; });

    detail::apply_necessity_overrides(deploy_yaml["necessity"], deployment, issues);

    std::vector<std::shared_ptr<detail::Bind>> all_binds;
    for (const auto& platform_p : platform_refs)
    {
        const auto& ref = platform_p.second;
        all_binds.insert(all_binds.end(), ref.binds.begin(), ref.binds.end());
        for (const auto& module_ref : ref.modules)
            for (const auto& run_ref : module_ref.run)
                all_binds.insert(all_binds.end(), run_ref.binds.begin(), run_ref.binds.end());
    }
    for (const auto& bind : all_binds)
    {
        if (bind->uses == 0)
            add_issue(issues, Issue::Severity::WARNING, "bind-unused",
                      "this bind matches no dynamic publication or subscription", bind->location);
    }

    return deployment;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
