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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
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

/// \brief Where to find interface files and the values of ${NAME} in a deployment file
struct LoadParams
{
    // searched, in order, after the deployment file's own directory
    std::vector<std::string> interface_dirs;
    // goby's own installed interfaces (share/goby/interfaces), searched after interface_dirs
    std::string goby_interfaces_dir;
    // ${NAME} values; these override the environment
    std::map<std::string, std::string> defines;
};

namespace detail
{
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
};

struct ModuleRef
{
    std::string name;
    std::vector<RunRef> run;
    bool not_a_module{false};
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
        if (entry.IsScalar())
        {
            (legacy ? ref.file : ref.application) = entry.as<std::string>();
        }
        else if (entry.IsMap())
        {
            static const std::set<std::string> legacy_keys{"file", "application"};
            static const std::set<std::string> run_keys{"application", "file", "name", "plugins"};
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
            }
            if (ref.file.empty() && ref.application.empty())
            {
                add_issue(issues, Issue::Severity::ERROR, "deployment-schema",
                          legacy ? "an interfaces entry that is a map must have file:"
                                 : "a run entry must name an application: or a file:",
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

    // generated interface files start with "application: <name>"; anything else is skipped
    static std::string declared_application(const std::string& path)
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

    std::vector<std::string> dirs_;
    bool indexed_{false};
    // application -> files declaring it, in search order
    std::map<std::string, std::vector<std::string>> index_;
    std::set<std::string> reported_shadowed_;
};

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
        add_issue(issues, Issue::Severity::WARNING, "variable-unset",
                  "${" + name + "} has no value (set it with -D " + name + "=value)");

    detail::InterfaceFinder finder(detail::search_dirs(path, params));
    std::map<std::string, std::vector<detail::ModuleRef>> platform_refs;

    if (!deploy_yaml.IsMap() || !deploy_yaml["deployment"])
    {
        if (deploy_yaml.IsMap() && deploy_yaml["application"])
        {
            deployment.name = deploy_yaml["application"].as<std::string>() + "_stub_deployment";
            detail::RunRef ref;
            ref.file = path;
            ref.location = "application";
            platform_refs["goby_platform"].push_back({"nomodule", {ref}, true});
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
                    platform_refs[platform_name].push_back(module_ref);
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
                platform_refs[platform_name] = {module_ref};
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
                     const detail::RunRef& ref)
    {
        try
        {
            app.merge(yaml, deployment.indexer);
        }
        catch (const std::exception& e)
        {
            add_issue(issues, Issue::Severity::ERROR, "interface-invalid", file + ": " + e.what(),
                      ref.location);
        }
    };

    for (const auto& platform_p : platform_refs)
    {
        Platform platform;
        platform.name = platform_p.first;
        for (const auto& module_ref : platform_p.second)
        {
            Module module;
            module.name = module_ref.name;
            module.not_a_module = module_ref.not_a_module;
            for (const auto& ref : module_ref.run)
            {
                auto file = locate(ref, ref.application, ref.file);
                YAML::Node yaml;
                if (file.empty() || !load_interface(file, ref, yaml))
                    continue;

                std::string app_name =
                    ref.name.empty() ? yaml["application"].as<std::string>() : ref.name;

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
                merge(it->second, yaml, file, ref);

                for (const auto& plugin : ref.plugins)
                {
                    bool is_file = std::filesystem::path(plugin).extension() == ".yml" ||
                                   std::filesystem::path(plugin).extension() == ".yaml";
                    auto plugin_file = locate(ref, is_file ? "" : plugin, is_file ? plugin : "");
                    YAML::Node plugin_yaml;
                    if (!plugin_file.empty() && load_interface(plugin_file, ref, plugin_yaml))
                        merge(it->second, plugin_yaml, plugin_file, ref);
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
