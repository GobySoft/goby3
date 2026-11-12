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

#ifndef GOBY_APPS_MIDDLEWARE_TEST_TOOL_VISUALIZE_H
#define GOBY_APPS_MIDDLEWARE_TEST_TOOL_VISUALIZE_H

#include <string>

#include "goby/apps/middleware/test_tool/test_tool_config.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
/// \brief Graphviz (dot) rendering of a test file's plan: fixtures, the required-application
/// gate, then each run and step in execution order with its publications, expectations and waits
std::string test_file_to_dot(const protobuf::TestFile& file, const std::string& title);

} // namespace middleware
} // namespace apps
} // namespace goby

#endif
