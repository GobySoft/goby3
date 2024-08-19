// Copyright 2024:
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

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"

#include "goby/apps/middleware/goby_tool/marshalling/protobuf.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
class ProtobufToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ProtobufToolConfig>
{
  public:
    ProtobufToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ProtobufToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ProtobufTool : public goby::middleware::Application<protobuf::ProtobufToolConfig>
{
  public:
    ProtobufTool();
    ~ProtobufTool() override {}

  private:
    void run() override { assert(false); }

  private:
};

class ProtobufShowToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ProtobufShowToolConfig>
{
  public:
    ProtobufShowToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ProtobufShowToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ProtobufShowTool : public goby::middleware::Application<protobuf::ProtobufShowToolConfig>,
                         public goby::middleware::ToolSharedLibraryLoader
{
  public:
    ProtobufShowTool();
    ~ProtobufShowTool() override {}

  private:
    void run() override { assert(false); }
};

} // namespace middleware
} // namespace apps
} // namespace goby
