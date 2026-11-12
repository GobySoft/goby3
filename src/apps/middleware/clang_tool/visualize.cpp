// Copyright 2020-2026:
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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "actions.h"
#include "goby/middleware/tool/interface/render_d2.h"
#include "goby/middleware/tool/interface/render_dot.h"

int goby::clang::visualize(const std::vector<std::string>& yamls, const VisualizeParameters& params)
{
    namespace interface = goby::middleware::tool::interface;

    interface::LoadParams load_params;
    load_params.expand_instances = params.expand_instances;
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
        load_params.goby_interfaces_dir =
            (exe.parent_path().parent_path() / "share" / "goby" / "interfaces").string();

    interface::Issues issues;
    auto deployment = interface::load_deployment(yamls.at(0), issues, load_params);
    for (const auto& issue : issues)
    {
        if (issue.severity != interface::Issue::Severity::INFO)
            std::cerr << issue << std::endl;
    }
    if (interface::has_errors(issues))
        return EXIT_FAILURE;

    if (params.format != "dot" && params.format != "d2")
    {
        std::cerr << "-format must be dot or d2" << std::endl;
        return EXIT_FAILURE;
    }

    std::string output_file = params.output_file;
    if (output_file.empty())
        output_file = deployment.name + "." + params.format;

    std::string file_name(params.output_directory + "/" + output_file);
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        return EXIT_FAILURE;
    }

    interface::ViewFilter::Params filter;
    filter.omit_group_regex = params.omit_group_regex;
    filter.omit_node_regex = params.omit_node_regex;
    filter.include_coroner = params.include_coroner;
    filter.include_terminate = params.include_terminate;
    filter.include_internal = params.include_internal;
    filter.omit_interthread = params.omit_interthread;
    filter.expand_goby_apps = params.expand_goby_apps;

    auto resolution = interface::resolve(deployment);
    if (params.format == "d2")
    {
        interface::D2Params d2_params;
        d2_params.filter = filter;
        d2_params.omit_disconnected = params.omit_disconnected;
        interface::write_d2(ofs, deployment, resolution, d2_params);
    }
    else
    {
        interface::DotParams dot_params;
        dot_params.filter = filter;
        dot_params.omit_disconnected = params.omit_disconnected;
        dot_params.splines = params.dot_splines;
        interface::write_dot(ofs, deployment, resolution, dot_params);
    }
    return 0;
}
