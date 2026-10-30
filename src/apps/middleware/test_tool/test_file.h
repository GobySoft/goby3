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

#ifndef GOBY_APPS_MIDDLEWARE_TEST_TOOL_TEST_FILE_H
#define GOBY_APPS_MIDDLEWARE_TEST_TOOL_TEST_FILE_H

#include <map>
#include <string>
#include <vector>

#include "goby/apps/middleware/test_tool/test_tool_config.pb.h"
#include "goby/middleware/protobuf/pubsub_test.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
using Issues = std::vector<goby::middleware::protobuf::PubSubTestIssue>;

/// \brief A test file with its includes merged, variables expanded and relative paths resolved
struct LoadedTestFile
{
    std::string path;
    protobuf::TestFile file;
    Issues issues;
    bool parsed{true}; // false if this file or an include could not be read or parsed
};

/// \brief Parses NAME=value entries; malformed entries are reported as issues
std::map<std::string, std::string>
parse_defines(const google::protobuf::RepeatedPtrField<std::string>& defines, Issues& issues);

/// \brief Replaces ${NAME} and ${NAME:-default} using defines, then TEST_DIR, then the
/// environment. Other names and "$${" escapes are left for expand_test_runs.
std::string expand_variables(const std::string& text,
                             const std::map<std::string, std::string>& defines,
                             const std::string& test_dir);

/// \brief Expands runs with repeat or for_each into one run per pass, reports any ${NAME} left
/// unresolved, and turns "$${" into a literal "${"
void expand_test_runs(protobuf::TestFile& file, const std::map<std::string, std::string>& defines,
                      Issues& issues);

/// \brief Loads a test file and everything it includes
LoadedTestFile load_test_file(const std::string& path,
                              const std::map<std::string, std::string>& defines);

/// \brief True when this build can drive zeromq endpoints
bool test_tool_has_zeromq();

/// \brief Adds the 'default' endpoint if none is declared and gives endpoints without a transport
/// the build's default transport
void finalize_endpoints(protobuf::TestFile& file);

/// \brief Loads the file's libraries (reporting failures as issues)
void load_test_libraries(const protobuf::TestFile& file, Issues& issues);

/// \brief Checks everything the file declares: endpoints, fixtures and the test itself
/// (libraries must already be loaded for type checks)
void validate_test_file(const protobuf::TestFile& file, Issues& issues);

} // namespace middleware
} // namespace apps
} // namespace goby

#endif
