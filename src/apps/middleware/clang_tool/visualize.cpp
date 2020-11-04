// Copyright 2020:
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

#include <fstream>
#include <iostream>

#include "actions.h"
#include "pubsub_entry.h"

#include <yaml-cpp/yaml.h>

using goby::clang::PubSubEntry;

goby::clang::VisualizeParameters g_params;
// node_name -> layer -> publish_index-> entry
std::map<std::string, std::map<goby::clang::Layer, std::map<int, PubSubEntry>>> g_pubs_in_use;

// node_name -> thread_display_name
std::map<std::string, std::shared_ptr<viz::Thread>> g_node_name_to_thread;

bool is_group_included(const std::string& group)
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

    if (internal_groups.count(group) && !g_params.include_internal)
        return false;
    else if (terminate_groups.count(group) && !g_params.include_terminate)
        return false;
    else if (coroner_groups.count(group) && !g_params.include_coroner)
        return false;
    else
        return true;
}

namespace viz
{
inline bool operator<(const Thread& a, const Thread& b) { return a.name < b.name; }

inline std::ostream& operator<<(std::ostream& os, const Thread& th)
{
    os << th.name << " | ";
    if (!th.interthread_publishes.empty() || !th.interthread_subscribes.empty())
    {
        using goby::clang::operator<<;
        for (const auto& p : th.interthread_publishes) os << "[PUB " << p << "]";
        for (const auto& s : th.interthread_subscribes) os << "[SUB " << s << "]";
    }
    return os;
}

struct Application
{
    // create from root interface.yml node
    Application(const YAML::Node& yaml, const std::string& override_name)
    {
        if (override_name.empty())
            name = yaml["application"].as<std::string>();
        else
            name = override_name;

        merge(yaml);
    }

    void merge(const YAML::Node& yaml)
    {
        auto interthread_node = yaml["interthread"];
        if (interthread_node)
        {
            auto threads_node = interthread_node["threads"];
            for (auto it = threads_node.begin(), end = threads_node.end(); it != end; ++it)
            {
                auto thread_node = *it;
                auto thread_name = thread_node["name"].as<std::string>();
                auto thread_known =
                    (!thread_node["known"]) ? true : thread_node["known"].as<bool>();
                auto bases_node = thread_node["bases"];
                std::set<std::string> bases;
                if (bases_node)
                {
                    for (const auto& base : bases_node) bases.insert(base.as<std::string>());
                }

                threads.emplace(thread_name, std::make_shared<Thread>(thread_name, thread_known,
                                                                      thread_node, bases));
            }

            // crosslink threads that aren't direct subclasses of goby::middleware::SimpleThread
            for (auto& thread_p : threads)
            {
                auto& bases = thread_p.second->bases;
                bool is_direct_thread_subclass = false;
                for (const auto& base : bases)
                {
                    if (base.find("goby::middleware::SimpleThread") == 0)
                        is_direct_thread_subclass = true;
                }

                if (!is_direct_thread_subclass)
                {
                    for (auto& base_thread_p : threads)
                    {
                        for (const auto& base : bases)
                        {
                            if (base == base_thread_p.first)
                            {
                                thread_p.second->child = base_thread_p.second;
                                base_thread_p.second->parent = thread_p.second;
                            }
                        }
                    }
                }
            }

            // after crosslinking, actually parse the yaml
            for (auto& thread_p : threads) { thread_p.second->parse_yaml(); }
        }

        auto interprocess_node = yaml["interprocess"];
        if (interprocess_node)
        {
            auto publish_node = interprocess_node["publishes"];
            for (auto p : publish_node)
                interprocess_publishes.emplace(goby::clang::Layer::INTERPROCESS,
                                               goby::clang::PubSubEntry::Direction::PUBLISH, p,
                                               threads);

            auto subscribe_node = interprocess_node["subscribes"];
            for (auto s : subscribe_node)
                interprocess_subscribes.emplace(goby::clang::Layer::INTERPROCESS,
                                                goby::clang::PubSubEntry::Direction::SUBSCRIBE, s,
                                                threads);
        }
        auto intervehicle_node = yaml["intervehicle"];
        if (intervehicle_node)
        {
            auto publish_node = intervehicle_node["publishes"];
            for (auto p : publish_node)
                intervehicle_publishes.emplace(goby::clang::Layer::INTERVEHICLE,
                                               goby::clang::PubSubEntry::Direction::PUBLISH, p,
                                               threads);

            auto subscribe_node = intervehicle_node["subscribes"];
            for (auto s : subscribe_node)
                intervehicle_subscribes.emplace(goby::clang::Layer::INTERVEHICLE,
                                                goby::clang::PubSubEntry::Direction::SUBSCRIBE, s,
                                                threads);
        }

        auto add_threads = [&](const std::set<PubSubEntry>& pubsubs) {
            for (const auto& e : pubsubs)
            {
                if (!threads.count(e.thread))
                    threads.emplace(e.thread,
                                    std::make_shared<Thread>(e.thread, e.thread_is_known));
            }
        };

        // add main thread name for applications without interthread pub/sub
        add_threads(interprocess_publishes);
        add_threads(interprocess_subscribes);
        add_threads(intervehicle_publishes);
        add_threads(intervehicle_subscribes);
    }

    std::string name;
    std::map<std::string, std::shared_ptr<Thread>> threads;
    std::set<PubSubEntry> interprocess_publishes;
    std::set<PubSubEntry> interprocess_subscribes;
    std::set<PubSubEntry> intervehicle_publishes;
    std::set<PubSubEntry> intervehicle_subscribes;
};

inline bool operator<(const Application& a, const Application& b) { return a.name < b.name; }

inline std::ostream& operator<<(std::ostream& os, const Application& a)
{
    os << a.name << " | ";
    os << "intervehicle: ";
    if (!a.intervehicle_publishes.empty() || !a.intervehicle_subscribes.empty())
    {
        using goby::clang::operator<<;
        for (const auto& p : a.intervehicle_publishes) os << "[PUB " << p << "]";
        for (const auto& s : a.intervehicle_subscribes) os << "[SUB " << s << "]";
    }
    else
    {
        os << "NONE";
    }
    os << " | interprocess: ";
    if (!a.interprocess_publishes.empty() || !a.interprocess_subscribes.empty())
    {
        using goby::clang::operator<<;
        for (const auto& p : a.interprocess_publishes) os << "[PUB " << p << "]";
        for (const auto& s : a.interprocess_subscribes) os << "[SUB " << s << "]";
    }
    else
    {
        os << "NONE";
    }

    if (!a.threads.empty())
    {
        os << " | ";
        os << "interthread: ";
        for (const auto& th_p : a.threads) os << "{" << th_p.second << "}";
    }

    return os;
}

struct PlatformParams
{
    std::string yaml;
    std::string application;
};

struct Platform
{
    Platform(const std::string& n, const std::vector<PlatformParams>& params) : name(n)
    {
        // each yaml represents a given application
        for (const auto& param : params)
        {
            YAML::Node yaml;
            try
            {
                yaml = YAML::LoadFile(param.yaml);
            }
            catch (const std::exception& e)
            {
                std::cout << "Failed to parse " << param.yaml << ": " << e.what() << std::endl;
            }

            Application app(yaml, param.application);

            auto it = applications.find(app.name);
            if (it == applications.end())
                applications.insert(std::make_pair(app.name, app));
            else
                it->second.merge(yaml);
        }
    }

    std::string name;
    std::map<std::string, Application> applications;
};

inline bool operator<(const Platform& a, const Platform& b) { return a.name < b.name; }

inline std::ostream& operator<<(std::ostream& os, const Platform& p)
{
    os << "((" << p.name << "))" << std::endl;
    for (const auto& a : p.applications) os << "Application: " << a.second << std::endl;
    return os;
}

struct Deployment
{
    Deployment(const std::string& n,
               const std::map<std::string, std::vector<PlatformParams>>& platform_params)
        : name(n)
    {
        for (const auto& platform_yaml_p : platform_params)
        { platforms.emplace(platform_yaml_p.first, platform_yaml_p.second); } }

    std::string name;
    std::set<Platform> platforms;
};

inline std::ostream& operator<<(std::ostream& os, const Deployment& d)
{
    os << "-----" << d.name << "-----" << std::endl;
    for (const auto& p : d.platforms) os << "Platform: " << p << std::endl;
    return os;
}

} // namespace viz

const auto vehicle_color = "darkgreen";
const auto process_color = "dodgerblue4";
const auto thread_color = "purple4";

const auto required_style = "bold";
const auto recommended_style = "tapered";
const auto optional_style = "solid";

std::string node_name(std::string p, std::string a, std::string th)
{
    std::vector<char> reserved{{':', '&', '<', '>', ' ', ',', '-'}};

    for (char r : reserved)
    {
        std::string search(1, r);
        auto replacement = "_" + std::to_string(static_cast<int>(r)) + "_";
        boost::replace_all(p, search, replacement);
        boost::replace_all(a, search, replacement);
        boost::replace_all(th, search, replacement);
    }

    return p + "_" + a + "_" + th;
}

std::string connection_with_label_final(const PubSubEntry& pub, std::string pub_str,
                                        std::string sub_str, std::string color,
                                        goby::middleware::Necessity necessity)
{
    g_pubs_in_use[pub_str][pub.layer].insert(std::make_pair(pub.publish_index, pub));

    std::string group = pub.group;
    std::string scheme = pub.scheme;
    std::string type = pub.type;

    viz::html_escape(group);
    viz::html_escape(scheme);
    viz::html_escape(type);

    std::string style;
    switch (necessity)
    {
        case goby::middleware::Necessity::REQUIRED: style = required_style; break;
        case goby::middleware::Necessity::RECOMMENDED: style = recommended_style; break;
        case goby::middleware::Necessity::OPTIONAL: style = optional_style; break;
    }

    // return pub_str + "->" + sub_str + "[xlabel=<<b><font point-size=\"10\">" + group +
    //        "</font></b><br/><font point-size=\"6\">" + scheme +
    //        "</font><br/><font point-size=\"8\">" + type + "</font>>" + ",color=" + color +
    //        ",style=" + style + "]\n";

    auto label = pub.publish_index_str();
    auto tooltip = label + ": " + pub.group + " | " + pub.scheme + " | " + pub.type;
    auto ret = pub_str + "->" + sub_str + "[fontsize=7,headlabel=\"" + label + "\",taillabel=\"" +
               label + "\",xlabel=\"" + label + ": " + group + "\",color=" + color +
               ",style=" + style + ",tooltip=\"" + tooltip + "\",labeltooltip=\"" + tooltip +
               "\",headtooltip=\"" + tooltip + "\",penwidth=0.5,arrowhead=vee,arrowsize=0.3]\n";

    return ret;
}

std::string connection_with_label(std::string pub_platform, std::string pub_application,
                                  const PubSubEntry& pub, std::string sub_platform,
                                  std::string sub_application, const PubSubEntry& sub,
                                  std::string color, goby::middleware::Necessity necessity)
{
    return connection_with_label_final(pub, node_name(pub_platform, pub_application, pub.thread),
                                       node_name(sub_platform, sub_application, sub.thread), color,
                                       necessity);
}

std::string disconnected_publication(std::string pub_platform, std::string pub_application,
                                     const PubSubEntry& pub, std::string color)
{
    if (g_params.omit_disconnected)
        return "";

    // hide inner publications without subscribers.
    if (pub.is_inner_pub)
        return "";
    else
        return node_name(pub_platform, pub_application, pub.thread) + "_no_subscribers_" + color +
               " [label=\"\",style=invis] \n" +
               connection_with_label_final(pub,
                                           node_name(pub_platform, pub_application, pub.thread),
                                           node_name(pub_platform, pub_application, pub.thread) +
                                               "_no_subscribers_" + color,
                                           color, goby::middleware::Necessity::OPTIONAL);
}

std::string disconnected_subscription(std::string sub_platform, std::string sub_application,
                                      const PubSubEntry& sub, std::string color,
                                      goby::middleware::Necessity necessity)
{
    if (g_params.omit_disconnected && necessity != goby::middleware::Necessity::REQUIRED)
        return "";

    if (necessity == goby::middleware::Necessity::REQUIRED)
        color = "red";

    auto pub_node =
        node_name(sub_platform, sub_application, sub.thread) + "_no_publishers_" + color;

    g_node_name_to_thread[pub_node] = std::make_shared<viz::Thread>();

    PubSubEntry fake_pub(sub.layer, PubSubEntry::Direction::PUBLISH, sub.thread, sub.group,
                         sub.scheme, sub.type, sub.thread_is_known, sub.necessity);

    g_pubs_in_use[pub_node][sub.layer].insert(std::make_pair(fake_pub.publish_index, fake_pub));

    return node_name(sub_platform, sub_application, sub.thread) + "_no_publishers_" + color +
           "  \n" +
           connection_with_label_final(
               fake_pub,
               node_name(sub_platform, sub_application, sub.thread) + "_no_publishers_" + color,
               node_name(sub_platform, sub_application, sub.thread), color, necessity);
}

void write_thread_connections(std::ofstream& ofs, const viz::Platform& platform,
                              const viz::Application& application, const viz::Thread& thread,
                              std::set<PubSubEntry>& disconnected_subs)
{
    std::set<PubSubEntry> disconnected_pubs;
    for (const auto& pub : thread.interthread_publishes)
    {
        if (!is_group_included(pub.group))
            continue;

        disconnected_pubs.insert(pub);

        for (const auto& sub_thread_p : application.threads)
        {
            const auto& sub_thread = sub_thread_p.second;
            for (const auto& sub : sub_thread->interthread_subscribes)
            {
                if (!is_group_included(sub.group))
                    continue;

                if (goby::clang::connects(pub, sub))
                {
                    remove_disconnected(pub, sub, disconnected_pubs, disconnected_subs);
                    ofs << "\t\t\t"
                        << connection_with_label(platform.name, application.name, pub,
                                                 platform.name, application.name, sub, thread_color,
                                                 sub.necessity)
                        << "\n";
                }
            }
        }
    }

    for (const auto& pub : disconnected_pubs)
        ofs << "\t\t\t"
            << disconnected_publication(platform.name, application.name, pub, thread_color) << "\n";
}

void write_process_connections(std::ofstream& ofs, const viz::Platform& platform,
                               const viz::Application& pub_application,
                               std::map<std::string, std::set<PubSubEntry>>& disconnected_subs)
{
    std::set<PubSubEntry> disconnected_pubs;

    for (const auto& pub : pub_application.interprocess_publishes)
    {
        if (!is_group_included(pub.group))
            continue;

        disconnected_pubs.insert(pub);
        for (const auto& sub_application : platform.applications)
        {
            for (const auto& sub : sub_application.second.interprocess_subscribes)
            {
                if (!is_group_included(sub.group))
                    continue;

                if (goby::clang::connects(pub, sub))
                {
                    remove_disconnected(pub, sub, disconnected_pubs,
                                        disconnected_subs[sub_application.second.name]);

                    ofs << "\t\t"
                        << connection_with_label(platform.name, pub_application.name, pub,
                                                 platform.name, sub_application.second.name, sub,
                                                 process_color, sub.necessity)
                        << "\n";
                }
            }
        }
    }

    for (const auto& pub : disconnected_pubs)
        ofs << "\t\t"
            << disconnected_publication(platform.name, pub_application.name, pub, process_color)
            << "\n";
}

void write_vehicle_connections(
    std::ofstream& ofs, const viz::Deployment& deployment, const viz::Platform& pub_platform,
    const viz::Application& pub_application,
    std::map<std::string, std::map<std::string, std::set<PubSubEntry>>>& disconnected_subs)
{
    std::set<PubSubEntry> disconnected_pubs;
    for (const auto& pub : pub_application.intervehicle_publishes)
    {
        if (!is_group_included(pub.group))
            continue;

        disconnected_pubs.insert(pub);
        for (const auto& sub_platform : deployment.platforms)
        {
            for (const auto& sub_application : sub_platform.applications)
            {
                for (const auto& sub : sub_application.second.intervehicle_subscribes)
                {
                    if (!is_group_included(sub.group))
                        continue;

                    if (goby::clang::connects(pub, sub))
                    {
                        remove_disconnected(
                            pub, sub, disconnected_pubs,
                            disconnected_subs[sub_platform.name][sub_application.second.name]);

                        ofs << "\t"
                            << connection_with_label(pub_platform.name, pub_application.name, pub,
                                                     sub_platform.name, sub_application.second.name,
                                                     sub, vehicle_color, sub.necessity)
                            << "\n";
                    }
                }
            }
        }
    }

    for (const auto& pub : disconnected_pubs)
        ofs << "\t"
            << disconnected_publication(pub_platform.name, pub_application.name, pub, vehicle_color)
            << "\n";
}

int goby::clang::visualize(const std::vector<std::string>& yamls, const VisualizeParameters& params)
{
    g_params = params;

    std::string deployment_name;

    // maps platform name to yaml files
    std::map<std::string, std::vector<viz::PlatformParams>> platform_params;

    // assume deployment file
    if (params.deployment.empty())
    {
        YAML::Node deploy_yaml;
        try
        {
            deploy_yaml = YAML::LoadFile(yamls.at(0));
        }
        catch (const std::exception& e)
        {
            std::cout << "Failed to parse deployment file: " << params.deployment << ": "
                      << e.what() << std::endl;
        }
        deployment_name = deploy_yaml["deployment"].as<std::string>();
        YAML::Node platforms_node = deploy_yaml["platforms"];
        if (!platforms_node || !platforms_node.IsSequence())
        {
            std::cerr << "Must specify platforms: as a sequence in deployment YAML file"
                      << std::endl;
            exit(EXIT_FAILURE);
        }

        for (auto platform : platforms_node)
        {
            std::string name = platform["name"].as<std::string>();
            YAML::Node interfaces_node = platform["interfaces"];

            if (!interfaces_node || !interfaces_node.IsSequence())
            {
                std::cerr << "Must specify interfaces: as a sequence in deployment YAML file for "
                             "platform: "
                          << name << std::endl;
                exit(EXIT_FAILURE);
            }

            for (auto interface_yaml : interfaces_node)
            {
                if (interface_yaml.IsMap())
                {
                    YAML::Node file_node = interface_yaml["file"];
                    if (!file_node)
                    {
                        std::cerr << "Must specify file: for each interfaces entry" << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    YAML::Node application_node = interface_yaml["application"];
                    platform_params[name].push_back(
                        {file_node.as<std::string>(),
                         application_node ? application_node.as<std::string>() : ""});
                }
                else if (interface_yaml.IsScalar())
                {
                    platform_params[name].push_back({interface_yaml.as<std::string>()});
                }
            }
        }
    }
    else
    {
        // degenerate case without deployment yml, e.g.
        // goby_clang_tool -viz -deployment example goby3_example_basic_interprocess_publisher_interface.yml goby3_example_basic_interprocess_subscriber_interface.yml

        // "params.deployment" is the name, not a file
        deployment_name = params.deployment;
        // use the yaml files passed as arguments to goby_clang_tool as if they belong to one deployment
        for (const auto& yaml : yamls) platform_params["default"].push_back({yaml});
    }

    viz::Deployment deployment(deployment_name, platform_params);

    std::string output_file = params.output_file;
    if (output_file.empty())
        output_file = deployment.name + ".dot";

    std::string file_name(params.output_directory + "/" + output_file);
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        exit(EXIT_FAILURE);
    }

    int cluster = 0;
    ofs << "digraph " << deployment.name << " { \n";
    ofs << "\tsplines=ortho\n";

    std::map<std::string, std::map<std::string, std::set<PubSubEntry>>> platform_disconnected_subs;
    for (const auto& sub_platform : deployment.platforms)
    {
        for (const auto& sub_application : sub_platform.applications)
        {
            for (const auto& sub : sub_application.second.intervehicle_subscribes)
            {
                if (!is_group_included(sub.group))
                    continue;
                platform_disconnected_subs[sub_platform.name][sub_application.second.name].insert(
                    sub);
            }
        }
    }

    for (const auto& platform : deployment.platforms)
    {
        ofs << "\tsubgraph cluster_" << cluster++ << " {\n";

        auto platform_display_name = platform.name;
        viz::html_escape(platform_display_name);

        ofs << "\t\tlabel=<<b>" << platform_display_name << "</b>>\n";
        ofs << "\t\tfontcolor=\"" << vehicle_color << "\"\n";
        ofs << "\t\tpenwidth=2\n";

        std::map<std::string, std::set<PubSubEntry>> process_disconnected_subs;
        for (const auto& application : platform.applications)
        {
            for (const auto& sub : application.second.interprocess_subscribes)
            {
                if (!is_group_included(sub.group))
                    continue;

                process_disconnected_subs[application.second.name].insert(sub);
            }
        }

        for (const auto& application : platform.applications)
        {
            ofs << "\t\tsubgraph cluster_" << cluster++ << " {\n";
            auto application_display_name = application.second.name;
            viz::html_escape(application_display_name);

            ofs << "\t\t\tlabel=<<b>" << application_display_name << "</b>>\n";
            ofs << "\t\t\tfontcolor=\"" << process_color << "\"\n";
            ofs << "\t\t\tpenwidth=2\n";

            std::set<PubSubEntry> thread_disconnected_subs;
            for (const auto& thread_p : application.second.threads)
            {
                const auto& thread = thread_p.second;

                for (const auto& sub : thread->interthread_subscribes)
                {
                    if (!is_group_included(sub.group))
                        continue;

                    thread_disconnected_subs.insert(sub);
                }
            }

            for (const auto& thread_p : application.second.threads)
            {
                const auto& thread = thread_p.second;

                write_thread_connections(ofs, platform, application.second, *thread,
                                         thread_disconnected_subs);

                auto node =
                    node_name(platform.name, application.second.name, thread->most_derived_name());
                g_node_name_to_thread[node] = thread;

                ofs << "\t\t\t" << node << "\n";
                // << " [label=<<font color=\"" << thread_color << "\">" << thread_display_name
                // << "</font><br/>" + pub_key + ">,shape=box,style="
                // << (thread->known ? "solid" : "dashed") << "]\n";
            }

            for (const auto& sub : thread_disconnected_subs)
            {
                ofs << "\t\t\t"
                    << disconnected_subscription(platform.name, application.second.name, sub,
                                                 thread_color, sub.necessity)
                    << "\n";
            }

            ofs << "\t\t}\n";

            write_process_connections(ofs, platform, application.second, process_disconnected_subs);
        }

        for (const auto& sub_p : process_disconnected_subs)
        {
            for (const auto& sub : sub_p.second)
            {
                ofs << "\t\t"
                    << disconnected_subscription(platform.name, sub_p.first, sub, process_color,
                                                 sub.necessity)
                    << "\n";
            }
        }

        ofs << "\t}\n";

        for (const auto& application : platform.applications)
            write_vehicle_connections(ofs, deployment, platform, application.second,
                                      platform_disconnected_subs);
    }

    for (const auto& sub_plat_p : platform_disconnected_subs)
    {
        for (const auto& sub_app_p : sub_plat_p.second)
        {
            for (const auto& sub : sub_app_p.second)
            {
                ofs << "\t"
                    << disconnected_subscription(sub_plat_p.first, sub_app_p.first, sub,
                                                 vehicle_color, sub.necessity)
                    << "\n";
            }
        }
    }

    for (const auto& node_thread_p : g_node_name_to_thread)
    {
        const auto& pub_str = node_thread_p.first;
        const auto& thread = node_thread_p.second;

        std::string thread_display_name = thread->most_derived_name();
        viz::html_escape(thread_display_name);

        std::string pub_key;
        for (const auto& layer_p : g_pubs_in_use[pub_str])
        {
            auto layer = layer_p.first;
            for (const auto& index_pub_p : layer_p.second)
            {
                const auto& pub = index_pub_p.second;

                std::string group = pub.group;
                std::string scheme = pub.scheme;
                std::string type = pub.type;

                viz::html_escape(group);
                viz::html_escape(scheme);
                viz::html_escape(type);

                std::string layer_color;
                switch (layer)
                {
                    case Layer::INTERTHREAD: layer_color = thread_color; break;
                    case Layer::UNKNOWN:
                    case Layer::INTERMODULE:
                    case Layer::INTERPROCESS: layer_color = process_color; break;
                    case Layer::INTERVEHICLE: layer_color = vehicle_color; break;
                }

                pub_key += "<font color=\"" + layer_color + "\" point-size=\"10\">" +
                           pub.publish_index_str() + ": </font><b><font point-size=\"10\">" +
                           group + "</font></b><br/><font point-size=\"6\">" + scheme +
                           "</font><br/><font point-size=\"8\">" + type + "</font><br/>";
            }
        }

        if (!thread->name.empty())
        {
            ofs << "\t" << pub_str << "\n"
                << " [label=<<font color=\"" << thread_color << "\">" << thread_display_name
                << "</font><br/>" + pub_key + ">,shape=box,style="
                << (thread->known ? "solid" : "dashed") << "]\n";
        }
        else
        {
            // disconnected subscribers
            ofs << "\t" << pub_str << "\n"
                << " [label=<" << pub_key << ">,penwidth=0]\n";
        }
    }

    ofs << "}\n";

    return 0;
}
