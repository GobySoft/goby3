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
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "actions.h"
#include "goby/middleware/tool/interface/render_dot.h"

int goby::clang::visualize(const std::vector<std::string>& yamls, const VisualizeParameters& params)
{
    namespace interface = goby::middleware::tool::interface;

    interface::Issues issues;
    auto deployment = interface::load_deployment(yamls.at(0), issues);
    for (const auto& issue : issues) std::cerr << issue << std::endl;
    if (interface::has_errors(issues))
        return EXIT_FAILURE;

    std::string output_file = params.output_file;
    if (output_file.empty())
        output_file = deployment.name + ".dot";

    std::string file_name(params.output_directory + "/" + output_file);
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        return EXIT_FAILURE;
    }

    interface::DotParams dot_params;
    dot_params.filter.omit_group_regex = params.omit_group_regex;
    dot_params.filter.omit_node_regex = params.omit_node_regex;
    dot_params.filter.include_coroner = params.include_coroner;
    dot_params.filter.include_terminate = params.include_terminate;
    dot_params.filter.include_internal = params.include_internal;
    dot_params.filter.omit_interthread = params.omit_interthread;
    dot_params.omit_disconnected = params.omit_disconnected;
    dot_params.splines = params.dot_splines;

    interface::write_dot(ofs, deployment, interface::resolve(deployment), dot_params);
    return 0;
}
