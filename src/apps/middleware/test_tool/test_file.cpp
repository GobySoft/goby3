// Copyright 2026:
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

#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "goby/middleware/marshalling/detail/dccl_serializer_parser.h"
#include "goby/middleware/tool/config_file.h"
#include "goby/middleware/tool/pubsub_test.h"

#include "fixture.h"
#include "test_file.h"

namespace fs = std::filesystem;
using goby::middleware::protobuf::PubSubTestIssue;

namespace
{
void add_issue(goby::apps::middleware::Issues& issues, const std::string& location,
               const std::string& message)
{
    PubSubTestIssue issue;
    issue.set_location(location);
    issue.set_message(message);
    issues.push_back(issue);
}

std::string resolve(const std::string& path, const std::string& dir)
{
    if (path.empty())
        return dir;
    fs::path p(path);
    return (p.is_absolute() ? p : fs::path(dir) / p).lexically_normal().string();
}

bool is_executable_file(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
           ::access(path.c_str(), X_OK) == 0;
}

bool on_path(const std::string& program)
{
    const char* path_env = std::getenv("PATH");
    std::stringstream ss(path_env ? path_env : "");
    std::string dir;
    while (std::getline(ss, dir, ':'))
        if (is_executable_file((dir.empty() ? std::string(".") : dir) + "/" + program))
            return true;
    return false;
}

// program the shell will run first, or empty when that can't be determined statically
std::string first_program(const std::string& command)
{
    static const std::set<std::string> skip{"exec", "nohup", "env", "setsid", "command"};
    // wrapper options that take a separate argument
    static const std::set<std::string> option_with_argument{"-u", "--unset", "-C", "--chdir", "-a"};
    static const std::set<std::string> builtin{
        "cd",    "export", "set", "unset", "true",   "false", ":",    ".",      "source",
        "eval",  "test",   "[",   "echo",  "printf", "if",    "for",  "while",  "case",
        "until", "{",      "(",   "trap",  "wait",   "kill",  "read", "ulimit", "umask"};
    static const std::regex word("^\\s*([^\\s;&|<>()]+)(.*)$");
    std::string rest = command;
    std::smatch m;
    while (std::regex_search(rest, m, word))
    {
        std::string w = m[1].str();
        std::string remainder = m[2].str();
        if (w.find('=') != std::string::npos && w.find('/') == std::string::npos)
        {
            rest = remainder; // VAR=value prefix
            continue;
        }
        if (skip.count(w))
        {
            rest = remainder;
            // skip the wrapper's own options
            while (std::regex_search(rest, m, word) && m[1].str().rfind("-", 0) == 0)
            {
                const bool takes_argument = option_with_argument.count(m[1].str()) > 0;
                rest = m[2].str();
                if (takes_argument && std::regex_search(rest, m, word))
                    rest = m[2].str();
            }
            continue;
        }
        if (builtin.count(w) || w.find('$') != std::string::npos ||
            w.find('`') != std::string::npos || w.find('"') != std::string::npos ||
            w.find('\'') != std::string::npos)
            return "";
        return w;
    }
    return "";
}

void load_recursive(const std::string& path, const std::map<std::string, std::string>& defines,
                    std::vector<std::string>& stack,
                    goby::apps::middleware::protobuf::TestFile& out,
                    goby::apps::middleware::Issues& issues, bool& parsed)
{
    std::error_code ec;
    fs::path canonical = fs::canonical(path, ec);
    if (ec)
    {
        add_issue(issues, path, "cannot open: " + ec.message());
        parsed = false;
        return;
    }
    const std::string key = canonical.string();
    for (const auto& s : stack)
        if (s == key)
        {
            add_issue(issues, path, "include cycle");
            parsed = false;
            return;
        }

    std::ifstream fin(key);
    std::stringstream ss;
    ss << fin.rdbuf();
    const std::string dir = canonical.parent_path().string();

    const std::string text = goby::apps::middleware::expand_variables(ss.str(), defines, dir);

    goby::apps::middleware::protobuf::TestFile local;
    try
    {
        goby::middleware::tool::parse_config_string(text, path, local);
    }
    catch (const std::exception& e)
    {
        add_issue(issues, path, e.what());
        parsed = false;
        return;
    }

    for (auto& lib : *local.mutable_load_shared_library()) lib = resolve(lib, dir);
    for (auto& fixture : *local.mutable_fixture()) fixture.set_cwd(resolve(fixture.cwd(), dir));

    stack.push_back(key);
    for (const auto& inc : local.include())
        load_recursive(resolve(inc, dir), defines, stack, out, issues, parsed);
    stack.pop_back();

    local.clear_include();
    out.MergeFrom(local);
}
} // namespace

std::map<std::string, std::string> goby::apps::middleware::parse_defines(
    const google::protobuf::RepeatedPtrField<std::string>& defines, Issues& issues)
{
    std::map<std::string, std::string> out;
    static const std::regex name("[A-Za-z_][A-Za-z0-9_]*");
    for (const auto& d : defines)
    {
        auto eq = d.find('=');
        if (eq == std::string::npos || !std::regex_match(d.substr(0, eq), name))
            add_issue(issues, "--define", "'" + d + "' is not of the form NAME=value");
        else
            out[d.substr(0, eq)] = d.substr(eq + 1);
    }
    return out;
}

std::string
goby::apps::middleware::expand_variables(const std::string& text,
                                         const std::map<std::string, std::string>& defines,
                                         const std::string& test_dir)
{
    static const std::regex var("^\\$\\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\\}");
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();)
    {
        // kept for run expansion, then unescaped
        if (text.compare(i, 3, "$${") == 0)
        {
            out += "$${";
            i += 3;
            continue;
        }

        std::smatch m;
        std::string tail;
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{')
        {
            tail =
                text.substr(i, text.find('}', i) == std::string::npos ? std::string::npos
                                                                      : text.find('}', i) - i + 1);
            if (std::regex_search(tail, m, var))
            {
                const std::string name = m[1].str();
                auto it = defines.find(name);
                const char* env = std::getenv(name.c_str());
                if (it != defines.end())
                    out += it->second;
                else if (name == "TEST_DIR")
                    out += test_dir;
                else if (env)
                    out += env;
                else if (m[2].matched)
                    out += m[3].str();
                else
                    out += m[0].str(); // a for_each name, or reported once runs are expanded
                i += m[0].length();
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

goby::apps::middleware::LoadedTestFile
goby::apps::middleware::load_test_file(const std::string& path,
                                       const std::map<std::string, std::string>& defines)
{
    LoadedTestFile loaded;
    loaded.path = path;
    std::vector<std::string> stack;
    load_recursive(path, defines, stack, loaded.file, loaded.issues, loaded.parsed);
    return loaded;
}

void goby::apps::middleware::expand_test_runs(protobuf::TestFile& file,
                                              const std::map<std::string, std::string>& defines,
                                              Issues& issues)
{
    // names already replaced when the file was read would silently never vary per pass
    auto set_outside = [&defines](const std::string& name)
    { return defines.count(name) || std::getenv(name.c_str()) || name == "TEST_DIR"; };
    bool any_passes = false;
    for (int i = 0, n = file.test().run_size(); i < n; ++i)
    {
        const auto& run = file.test().run(i);
        any_passes = any_passes || run.repeat() != 1 || run.for_each_size() > 0;
        std::set<std::string> reported;
        for (int j = 0, m = run.for_each_size(); j < m; ++j)
            for (const auto& v : run.for_each(j).var())
                if (set_outside(v.key()) && reported.insert(v.key()).second)
                    add_issue(issues,
                              "test.run[" + std::to_string(i) + "].for_each[" + std::to_string(j) +
                                  "]",
                              "'" + v.key() +
                                  "' is also set by --define, the environment or TEST_DIR, which "
                                  "replace it when the file is read; use another name");
    }
    if (any_passes && set_outside("ITERATION"))
        add_issue(issues, "test.run",
                  "ITERATION is set by --define or the environment, which replaces ${ITERATION} "
                  "when the file is read; unset it");

    // checked before expansion so locations match the file; each run may use its own names
    protobuf::TestFile outside_runs = file;
    outside_runs.mutable_test()->clear_run();
    goby::middleware::tool::find_unresolved_variables(outside_runs, "", issues);
    for (int i = 0, n = file.test().run_size(); i < n; ++i)
    {
        std::set<std::string> allowed{"ITERATION"};
        for (const auto& p : file.test().run(i).for_each())
            for (const auto& v : p.var()) allowed.insert(v.key());
        auto run = file.test().run(i);
        run.clear_for_each();
        goby::middleware::tool::find_unresolved_variables(
            run, "test.run[" + std::to_string(i) + "]", issues, allowed);
    }

    goby::middleware::tool::expand_runs(*file.mutable_test(), issues);
    goby::middleware::tool::unescape_variables(file);
}

bool goby::apps::middleware::test_tool_has_zeromq()
{
#ifdef GOBY_TEST_TOOL_HAS_ZEROMQ
    return true;
#else
    return false;
#endif
}

void goby::apps::middleware::finalize_endpoints(protobuf::TestFile& file)
{
    if (file.endpoint_size() == 0)
    {
        auto& ep = *file.add_endpoint();
        ep.set_name("default");
    }
    for (auto& ep : *file.mutable_endpoint())
    {
        if (ep.transport_case() == protobuf::TestEndpoint::TRANSPORT_NOT_SET)
        {
            if (test_tool_has_zeromq())
                ep.mutable_zeromq();
            else
                ep.mutable_udpm();
        }
    }
}

void goby::apps::middleware::load_test_libraries(const protobuf::TestFile& file, Issues& issues)
{
    for (int i = 0, n = file.load_shared_library_size(); i < n; ++i)
    {
        const auto& lib = file.load_shared_library(i);
        const std::string loc = "load_shared_library[" + std::to_string(i) + "]";
        if (!fs::exists(lib))
        {
            add_issue(issues, loc, "'" + lib + "' does not exist");
            continue;
        }
        try
        {
            goby::middleware::detail::DCCLSerializerParserHelperBase::load_library(lib);
        }
        catch (const std::exception& e)
        {
            add_issue(issues, loc, e.what());
        }
    }
}

void goby::apps::middleware::validate_test_file(const protobuf::TestFile& file, Issues& issues)
{
    std::set<std::string> endpoint_names;
    for (int i = 0, n = file.endpoint_size(); i < n; ++i)
    {
        const auto& ep = file.endpoint(i);
        const std::string loc = "endpoint[" + std::to_string(i) + "]";
        if (ep.name().empty())
            add_issue(issues, loc + ".name", "must not be empty");
        else if (!endpoint_names.insert(ep.name()).second)
            add_issue(issues, loc + ".name", "duplicate endpoint name '" + ep.name() + "'");
        if (ep.layer() != goby::middleware::protobuf::LAYER_INTERPROCESS &&
            ep.layer() != goby::middleware::protobuf::LAYER_INTERMODULE)
            add_issue(issues, loc + ".layer",
                      "only LAYER_INTERPROCESS and LAYER_INTERMODULE are supported");
        if (ep.has_zeromq() && !test_tool_has_zeromq())
            add_issue(issues, loc + ".zeromq", "this build of goby has no zeromq support");
    }

    std::set<std::string> fixture_names;
    for (int i = 0, n = file.fixture_size(); i < n; ++i)
    {
        const auto& f = file.fixture(i);
        const std::string loc = "fixture[" + std::to_string(i) + "]";
        if (f.name().empty())
            add_issue(issues, loc + ".name", "must not be empty");
        else if (!fixture_names.insert(f.name()).second)
            add_issue(issues, loc + ".name", "duplicate fixture name '" + f.name() + "'");

        if (!fs::is_directory(f.cwd()))
            add_issue(issues, loc + ".cwd", "'" + f.cwd() + "' is not a directory");

        if (f.command().empty())
            add_issue(issues, loc + ".command", "must not be empty");
        else
        {
            const std::string program = first_program(f.command());
            if (!program.empty())
            {
                const bool found = program.find('/') != std::string::npos
                                       ? is_executable_file(resolve(program, f.cwd()))
                                       : on_path(program);
                if (!found)
                    add_issue(issues, loc + ".command",
                              "'" + program + "' is not an executable file or on the PATH");
            }
        }

        for (int j = 0, m = f.env_size(); j < m; ++j)
            if (f.env(j).find('=') == std::string::npos)
                add_issue(issues, loc + ".env[" + std::to_string(j) + "]",
                          "'" + f.env(j) + "' is not of the form NAME=value");

        for (int j = 0, m = f.ready().output_regex_size(); j < m; ++j)
        {
            try
            {
                std::regex r(f.ready().output_regex(j));
            }
            catch (const std::regex_error& e)
            {
                add_issue(issues, loc + ".ready.output_regex[" + std::to_string(j) + "]",
                          std::string("invalid regular expression: ") + e.what());
            }
        }
        if (f.ready().timeout() <= 0)
            add_issue(issues, loc + ".ready.timeout", "must be greater than zero");
        if (f.ready().delay() < 0)
            add_issue(issues, loc + ".ready.delay", "must not be negative");
        if (signal_from_name(f.stop().signal()) == 0)
            add_issue(issues, loc + ".stop.signal", "unknown signal '" + f.stop().signal() + "'");
        if (f.stop().grace() < 0)
            add_issue(issues, loc + ".stop.grace", "must not be negative");
    }

    if (!file.has_test())
    {
        add_issue(issues, "test", "no test is defined");
        return;
    }

    auto test_issues =
        goby::middleware::tool::PubSubTestValidator(file.test(), endpoint_names).validate();
    issues.insert(issues.end(), test_issues.begin(), test_issues.end());
}
