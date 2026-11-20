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

#ifndef GOBY_MIDDLEWARE_TOOL_INTERFACE_LANGUAGE_INTERTHREAD_H
#define GOBY_MIDDLEWARE_TOOL_INTERFACE_LANGUAGE_INTERTHREAD_H

#include <algorithm>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace goby
{
namespace middleware
{
namespace tool
{
namespace interface
{
/// \brief The interthread layer of an application written in Julia or Python
///
/// The bindings carry interthread in their own language, so it never appears in the C++ that
/// goby_clang_tool reads. Their generators write it to a manifest instead, which
/// goby_clang_tool -gen adds to the interface file as one of these sections.
struct LanguageInterthread
{
    // top-level key in the manifest and the interface file
    std::string section;
    // the key naming the task or thread an entry belongs to
    std::string thread_key;
    // the scheme its entries are drawn and matched as
    std::string scheme;
    // subscriptions may give capacity and overflow
    bool has_delivery;
};

inline const std::vector<LanguageInterthread>& language_interthreads()
{
    static const std::vector<LanguageInterthread> languages{
        {"julia_interthread", "task", "JULIA", true},
        {"python_interthread", "thread", "PYTHON", false}};
    return languages;
}

/// \brief The thread an entry without one is drawn on: it applies to any thread
inline const std::string& any_language_thread()
{
    static const std::string name("any thread");
    return name;
}

/// \brief Checks one language's section; returns what is wrong with it, or an empty string
inline std::string check_language_interthread(const YAML::Node& section,
                                              const LanguageInterthread& language)
{
    auto where = language.section;
    if (!section.IsMap())
        return "'" + where + "' must be a mapping";

    for (const auto& kv : section)
    {
        auto key = kv.first.as<std::string>();
        if (key != "publishes" && key != "subscribes")
            return "unknown key '" + where + "." + key + "'";
        if (!kv.second.IsNull() && !kv.second.IsSequence())
            return "'" + where + "." + key + "' must be a list";

        std::vector<std::string> allowed{language.thread_key, "group", "type"};
        if (key == "subscribes")
        {
            allowed.push_back("necessity");
            if (language.has_delivery)
            {
                allowed.push_back("capacity");
                allowed.push_back("overflow");
            }
        }

        for (const auto& entry : kv.second)
        {
            auto entry_where = where + "." + key;
            if (!entry.IsMap())
                return "each entry of '" + entry_where + "' must be a mapping";
            for (const auto& field : entry)
            {
                auto name = field.first.as<std::string>();
                if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
                    return "unknown key '" + entry_where + "." + name + "'";
                if (!field.second.IsScalar())
                    return "'" + entry_where + "." + name + "' must be a single value";
            }
            for (const auto* required : {"group", "type"})
            {
                if (!entry[required])
                    return "an entry of '" + entry_where + "' has no '" + required + "'";
            }

            if (auto necessity = entry["necessity"])
            {
                auto n = necessity.as<std::string>();
                if (n != "required" && n != "recommended" && n != "optional")
                    return "'" + entry_where + ".necessity' is '" + n +
                           "'; it is one of required, recommended, optional";
            }
            if (auto overflow = entry["overflow"])
            {
                auto o = overflow.as<std::string>();
                if (o != "block" && o != "drop_oldest" && o != "drop_newest" && o != "error")
                    return "'" + entry_where + ".overflow' is '" + o +
                           "'; it is one of block, drop_oldest, drop_newest, error";
            }
            if (auto capacity = entry["capacity"])
            {
                int value = 0;
                if (!YAML::convert<int>::decode(capacity, value) || value < 1)
                    return "'" + entry_where + ".capacity' must be a positive integer";
            }
        }
    }
    return "";
}

} // namespace interface
} // namespace tool
} // namespace middleware
} // namespace goby

#endif
