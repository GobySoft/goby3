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

#ifndef GOBY_APPS_MIDDLEWARE_GOBY_TOOL_TEST_TOOL_H
#define GOBY_APPS_MIDDLEWARE_GOBY_TOOL_TEST_TOOL_H

#include "goby/middleware/application/configurator.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"

#include "goby/apps/middleware/goby_tool/test.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
class TestToolConfigurator : public goby::middleware::ProtobufConfigurator<protobuf::TestToolConfig>
{
  public:
    TestToolConfigurator(int argc, char* argv[]);

  private:
    void validate() const override;

    std::string error_;
};

class TestTool : public goby::middleware::Application<protobuf::TestToolConfig>,
                 public goby::middleware::ToolSharedLibraryLoader
{
  public:
    TestTool() : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library()) {}
    ~TestTool() override {}

  private:
    void run() override;
};

} // namespace middleware
} // namespace apps
} // namespace goby

#endif
