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
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

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

    Severity severity{Severity::ERROR};
    std::string code;
    std::string message;
    // e.g. "platforms[0].interfaces[2]"
    std::string location;
};

using Issues = std::vector<Issue>;

inline void add_issue(Issues& issues, Issue::Severity severity, std::string code,
                      std::string message, std::string location = "")
{
    issues.push_back({severity, std::move(code), std::move(message), std::move(location)});
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
    void merge(const YAML::Node& yaml, PublishIndexer& indexer);

  private:
    std::string most_derived(const std::string& thread) const
    {
        auto it = threads.find(thread);
        return it == threads.end() ? thread : it->second->most_derived_name();
    }

    void insert(std::set<PubSubEntry>& entries, PubSubEntry entry, PublishIndexer& indexer)
    {
        auto result = entries.insert(entry);
        if (result.second && entry.direction == PubSubEntry::Direction::PUBLISH)
            result.first->publish_index = indexer.next(entry.layer);
    }
};

inline void Application::merge(const YAML::Node& yaml, PublishIndexer& indexer)
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
                       indexer);
            for (const auto& s : thread_yaml.second["subscribes"])
                insert(thread.interthread_subscribes,
                       PubSubEntry(Layer::INTERTHREAD, Direction::SUBSCRIBE, s,
                                   thread.most_derived_name()),
                       indexer);
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
            insert(publishes[layer], entry, indexer);
        }
        for (const auto& s : layer_node["subscribes"])
        {
            PubSubEntry entry(layer, Direction::SUBSCRIBE, s);
            entry.thread = most_derived(entry.thread);
            insert(subscribes[layer], entry, indexer);
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

struct Platform
{
    std::string name;
    std::vector<Module> modules;

    bool has_modules() const { return !(modules.size() == 1 && modules.front().not_a_module); }
};

struct Deployment
{
    std::string name;
    std::vector<Platform> platforms;
    // continues numbering after the loaded publications, for placeholders added when drawing
    PublishIndexer indexer;
};

namespace detail
{
struct InterfaceRef
{
    std::string file;
    std::string application;
    std::string location;
};

struct ModuleRef
{
    std::string name;
    std::vector<InterfaceRef> interfaces;
    bool not_a_module{false};
};

inline std::vector<InterfaceRef> read_interface_refs(const YAML::Node& interfaces_node,
                                                     const std::string& location, Issues& issues)
{
    std::vector<InterfaceRef> refs;
    int index = 0;
    for (const auto& interface_yaml : interfaces_node)
    {
        std::string entry_location = location + "[" + std::to_string(index++) + "]";
        if (interface_yaml.IsMap())
        {
            if (!interface_yaml["file"])
            {
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          "each interfaces entry that is a map must have file:", entry_location);
                continue;
            }
            auto application_node = interface_yaml["application"];
            refs.push_back({interface_yaml["file"].as<std::string>(),
                            application_node ? application_node.as<std::string>() : "",
                            entry_location});
        }
        else if (interface_yaml.IsScalar())
        {
            refs.push_back({interface_yaml.as<std::string>(), "", entry_location});
        }
    }
    return refs;
}

inline bool load_yaml(const std::string& path, const std::string& code, const std::string& location,
                      Issues& issues, YAML::Node& yaml)
{
    try
    {
        yaml = YAML::LoadFile(path);
        return true;
    }
    catch (const YAML::BadFile&)
    {
        add_issue(issues, Issue::Severity::ERROR, code + "-missing", "cannot open " + path,
                  location);
    }
    catch (const std::exception& e)
    {
        add_issue(issues, Issue::Severity::ERROR, code + "-invalid",
                  "cannot parse " + path + ": " + e.what(), location);
    }
    return false;
}

} // namespace detail

/// \brief Load a deployment file, or a single interface file as a one-platform stub deployment
///
/// Problems are added to \c issues; on an error the returned deployment may be incomplete.
inline Deployment load_deployment(const std::string& path, Issues& issues)
{
    Deployment deployment;

    YAML::Node deploy_yaml;
    if (!detail::load_yaml(path, "deployment", "", issues, deploy_yaml))
        return deployment;

    std::map<std::string, std::vector<detail::ModuleRef>> platform_refs;

    if (!deploy_yaml["deployment"])
    {
        if (deploy_yaml["application"])
        {
            deployment.name = deploy_yaml["application"].as<std::string>() + "_stub_deployment";
            platform_refs["goby_platform"].push_back(
                {"nomodule", {{path, "", "application"}}, true});
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

            if (auto modules_node = platform["modules"])
            {
                int module_index = 0;
                for (const auto& module : modules_node)
                {
                    std::string module_location =
                        location + ".modules[" + std::to_string(module_index++) + "]";
                    auto interfaces_node = module["interfaces"];
                    if (!module["name"] || !interfaces_node || !interfaces_node.IsSequence())
                    {
                        add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                                  "each module must have name: and an interfaces: sequence",
                                  module_location);
                        continue;
                    }
                    platform_refs[platform_name].push_back(
                        {module["name"].as<std::string>(),
                         detail::read_interface_refs(interfaces_node,
                                                     module_location + ".interfaces", issues),
                         false});
                }
            }
            else
            {
                auto interfaces_node = platform["interfaces"];
                if (!interfaces_node || !interfaces_node.IsSequence())
                {
                    add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                              "a platform without modules: must have an interfaces: sequence",
                              location);
                    continue;
                }
                platform_refs[platform_name] = {
                    {"nomodule",
                     detail::read_interface_refs(interfaces_node, location + ".interfaces", issues),
                     true}};
            }
        }
    }

    for (const auto& platform_p : platform_refs)
    {
        Platform platform;
        platform.name = platform_p.first;
        for (const auto& module_ref : platform_p.second)
        {
            Module module;
            module.name = module_ref.name;
            module.not_a_module = module_ref.not_a_module;
            for (const auto& ref : module_ref.interfaces)
            {
                YAML::Node yaml;
                if (!detail::load_yaml(ref.file, "interface", ref.location, issues, yaml))
                    continue;

                std::string app_name = ref.application;
                if (app_name.empty())
                {
                    if (!yaml["application"])
                    {
                        add_issue(issues, Issue::Severity::ERROR, "interface-invalid",
                                  ref.file + " has no application:", ref.location);
                        continue;
                    }
                    app_name = yaml["application"].as<std::string>();
                }

                auto it = module.applications.find(app_name);
                if (it == module.applications.end())
                    it = module.applications.emplace(app_name, Application(app_name)).first;
                try
                {
                    it->second.merge(yaml, deployment.indexer);
                }
                catch (const std::exception& e)
                {
                    add_issue(issues, Issue::Severity::ERROR, "interface-invalid",
                              ref.file + ": " + e.what(), ref.location);
                }
            }
            platform.modules.push_back(std::move(module));
        }
        std::stable_sort(platform.modules.begin(), platform.modules.end(),
                         [](const Module& a, const Module& b) { return a.name < b.name; });
        deployment.platforms.push_back(std::move(platform));
    }

    return deployment;
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
