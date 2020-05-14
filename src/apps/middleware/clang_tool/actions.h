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

#ifndef GENERATE_20190801H
#define GENERATE_20190801H

#include <string>
#include <vector>

namespace clang
{
namespace tooling
{
class ClangTool;
}
} // namespace clang

namespace goby
{
namespace clang
{
int generate(::clang::tooling::ClangTool& Tool, std::string output_directory,
             std::string output_file, std::string target_name);

struct VisualizeParameters
{
    std::string output_directory;
    std::string output_file;
    std::string deployment;
    bool omit_disconnected;
    bool include_coroner;
    bool include_terminate;
    bool include_internal;
};

int visualize(const std::vector<std::string>& ymls, const VisualizeParameters& params);
} // namespace clang
} // namespace goby

#endif
