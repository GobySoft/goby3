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

#ifndef GOBY_APPS_MIDDLEWARE_INTERFACE_TOOL_INSPECT_H
#define GOBY_APPS_MIDDLEWARE_INTERFACE_TOOL_INSPECT_H

#include <string>

#include "goby/middleware/tool/interface/resolve.h"

#include "goby/apps/middleware/interface_tool/interface_tool_config.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
namespace interface_tool
{
protobuf::InterfaceList list(const std::string& file,
                             const goby::middleware::tool::interface::Deployment& deployment);
std::string render_human(const protobuf::InterfaceList& list,
                         protobuf::InterfaceListConfig::What what);

protobuf::InterfaceExplanation
explain(const std::string& file, const goby::middleware::tool::interface::Deployment& deployment,
        const goby::middleware::tool::interface::Resolution& resolution,
        const std::string& group_regex, const std::string& type_regex);
std::string render_human(const protobuf::InterfaceExplanation& explanation);

protobuf::InterfaceDiff diff(const std::string& file_a,
                             const goby::middleware::tool::interface::Deployment& a,
                             const std::string& file_b,
                             const goby::middleware::tool::interface::Deployment& b);
bool has_differences(const protobuf::InterfaceDiff& diff);
std::string render_human(const protobuf::InterfaceDiff& diff);

/// \brief A deployment file running every application declared in \c directory on one platform
///
/// \param applications set to the number of applications found
std::string init(const std::string& directory, const std::string& platform,
                 const std::string& deployment_name, int& applications);

} // namespace interface_tool
} // namespace middleware
} // namespace apps
} // namespace goby

#endif
