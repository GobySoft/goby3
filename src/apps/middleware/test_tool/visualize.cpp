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

#include <sstream>
#include <utility>
#include <vector>

#include "visualize.h"

using goby::apps::middleware::protobuf::TestFile;
using namespace goby::middleware::protobuf;

namespace
{
std::string escape(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
            out += "\\l";
        else
            out += c;
    }
    return out;
}

std::string truncate(const std::string& s, std::size_t n = 48)
{
    std::string one_line;
    for (char c : s) one_line += (c == '\n' ? ' ' : c);
    return one_line.size() > n ? one_line.substr(0, n - 3) + "..." : one_line;
}

std::string number(double d)
{
    std::ostringstream ss;
    ss << d;
    return ss.str();
}

class DotWriter
{
  public:
    DotWriter(const TestFile& file) : file_(file)
    {
        if (file.endpoint_size() > 0)
            default_endpoint_ = file.endpoint(0).name();
    }

    std::string write(const std::string& title)
    {
        out_ << "digraph goby_test {\n"
             << "  graph [rankdir=TB, fontname=\"Helvetica\", fontsize=12, labelloc=t, label=\""
             << escape(title) << "\"];\n"
             << "  node [shape=box, style=rounded, fontname=\"Helvetica\", fontsize=10];\n"
             << "  edge [fontname=\"Helvetica\", fontsize=9];\n"
             << "  start [shape=circle, label=\"\", width=0.2, style=filled, fillcolor=black];\n";

        std::string prev = "start";

        if (file_.fixture_size() > 0)
        {
            // edges are written after the cluster so 'start' isn't drawn inside it
            std::vector<std::pair<std::string, std::string>> fixture_edges;
            out_ << "  subgraph cluster_fixtures {\n    label=\"fixtures\"; style=dashed;\n";
            for (int i = 0, n = file_.fixture_size(); i < n; ++i)
            {
                const auto& f = file_.fixture(i);
                std::string label = f.name() + "\n$ " + truncate(f.command(), 60) + "\n";
                if (f.ready().has_output_regex())
                    label += "ready: /" + f.ready().output_regex() + "/ within " +
                             number(f.ready().timeout()) + " s\n";
                if (f.ready().delay() > 0)
                    label += "then wait " + number(f.ready().delay()) + " s\n";
                const std::string id = "fixture_" + std::to_string(i);
                out_ << "    " << id << " [shape=component, label=\"" << escape(label) << "\"];\n";
                fixture_edges.push_back({prev, id});
                prev = id;
            }
            out_ << "  }\n";
            for (const auto& e : fixture_edges) edge(e.first, e.second);
        }

        if (file_.has_test())
        {
            const auto& test = file_.test();
            for (int i = 0, n = test.require_app_size(); i < n; ++i)
            {
                const auto& req = test.require_app(i);
                std::string label = "require_app " + endpoint(req.endpoint()) + "\n";
                for (const auto& name : req.name()) label += "  " + name + "\n";
                label += "within " + number(req.timeout()) + " s\n";
                const std::string id = "require_app_" + std::to_string(i);
                out_ << "  " << id << " [shape=hexagon, label=\"" << escape(label) << "\"];\n";
                edge(prev, id);
                prev = id;
            }

            std::vector<std::pair<std::string, std::string>> runs;
            for (int i = 0, n = test.run_size(); i < n; ++i)
                runs.push_back(write_run(test.run(i), i, test.default_timeout()));
            prev = connect(prev, runs, test.mode(), "top", "  ");
        }

        out_ << "  end [shape=doublecircle, label=\"\", width=0.15, style=filled, "
                "fillcolor=black];\n";
        edge(prev, "end");
        out_ << "}\n";
        return out_.str();
    }

  private:
    std::string endpoint(const std::string& name) const
    {
        return "[" + (name.empty() ? default_endpoint_ : name) + "]";
    }

    void edge(const std::string& from, const std::string& to)
    {
        out_ << "  " << from << " -> " << to << ";\n";
    }

    std::string bar(const std::string& id, const std::string& indent)
    {
        out_
            << indent << id
            << " [shape=box, style=filled, fillcolor=black, label=\"\", height=0.04, width=1.2];\n";
        return id;
    }

    // chains (SEQUENTIAL) or forks and joins (PARALLEL) the given entry/exit pairs after `prev`
    std::string connect(const std::string& prev,
                        const std::vector<std::pair<std::string, std::string>>& parts,
                        PubSubTestRunMode mode, const std::string& tag, const std::string& indent)
    {
        if (parts.empty())
            return prev;
        if (mode == PARALLEL && parts.size() > 1)
        {
            const std::string fork = bar("fork_" + tag, indent);
            const std::string join = bar("join_" + tag, indent);
            edge(prev, fork);
            for (const auto& p : parts)
            {
                edge(fork, p.first);
                edge(p.second, join);
            }
            return join;
        }
        std::string last = prev;
        for (const auto& p : parts)
        {
            edge(last, p.first);
            last = p.second;
        }
        return last;
    }

    std::string count_text(const PubSubTestExpect& e) const
    {
        if (e.has_count())
            return e.count() == 0 ? "none" : "exactly " + std::to_string(e.count());
        std::string s = ">= " + std::to_string(e.min_count());
        if (e.has_max_count())
            s += ", <= " + std::to_string(e.max_count());
        return s;
    }

    template <typename Leaf>
    std::string leaf_label(const Leaf& leaf, const std::string& fallback_name, double timeout)
    {
        std::string label = (leaf.name().empty() ? fallback_name : leaf.name()) + "\n";
        if (leaf.wait() > 0)
            label += "wait " + number(leaf.wait()) + " s\n";
        for (const auto& p : leaf.publish())
        {
            label += "publish " + endpoint(p.endpoint()) + " " + p.group() + " : " + p.type();
            if (p.repeat() > 1)
                label +=
                    " x" + std::to_string(p.repeat()) + " every " + number(p.interval()) + " s";
            if (p.delay() > 0)
                label += " after " + number(p.delay()) + " s";
            label += "\n  " + truncate(p.value()) + "\n";
        }
        for (const auto& e : leaf.expect())
        {
            label += "expect " + endpoint(e.endpoint()) + " " + e.group() + " : " + e.type() +
                     "\n  " + count_text(e) + " within " +
                     number(e.has_timeout() ? e.timeout() : timeout) + " s\n";
            if (e.has_match())
                label += "  match " + truncate(e.match()) + "\n";
            for (const auto& c : e.condition())
                label += "  where " + c.field() + " " + PubSubTestCondition::Op_Name(c.op()) +
                         (c.has_value() ? " " + truncate(c.value(), 32) : "") + "\n";
        }
        return label;
    }

    std::pair<std::string, std::string> write_run(const PubSubTestRun& run, int i,
                                                  double default_timeout)
    {
        const double timeout = run.has_timeout() ? run.timeout() : default_timeout;
        const std::string run_name =
            run.name().empty() ? "run " + std::to_string(i + 1) : run.name();
        const std::string id = "run_" + std::to_string(i);

        if (run.step_size() == 0)
        {
            out_ << "  " << id << " [label=\"" << escape(leaf_label(run, run_name, timeout))
                 << "\"];\n";
            return {id, id};
        }

        out_ << "  subgraph cluster_" << id << " {\n    label=\""
             << escape(run_name + (run.mode() == PARALLEL ? " (parallel)" : " (sequential)"))
             << "\"; style=rounded;\n";
        std::vector<std::pair<std::string, std::string>> steps;
        for (int j = 0, m = run.step_size(); j < m; ++j)
        {
            const auto& step = run.step(j);
            const std::string sid = id + "_step_" + std::to_string(j);
            const double step_timeout = step.has_timeout() ? step.timeout() : timeout;
            out_ << "    " << sid << " [label=\""
                 << escape(leaf_label(step, "step " + std::to_string(j + 1), step_timeout))
                 << "\"];\n";
            steps.push_back({sid, sid});
        }

        std::string entry = steps.front().first, exit = steps.back().second;
        if (run.mode() == PARALLEL && steps.size() > 1)
        {
            entry = bar("fork_" + id, "    ");
            exit = bar("join_" + id, "    ");
            for (const auto& s : steps)
            {
                edge(entry, s.first);
                edge(s.second, exit);
            }
        }
        else
        {
            for (std::size_t k = 1; k < steps.size(); ++k)
                edge(steps[k - 1].second, steps[k].first);
        }
        out_ << "  }\n";
        return {entry, exit};
    }

    const TestFile& file_;
    std::string default_endpoint_;
    std::ostringstream out_;
};
} // namespace

std::string goby::apps::middleware::test_file_to_dot(const TestFile& file, const std::string& title)
{
    return DotWriter(file).write(title);
}
