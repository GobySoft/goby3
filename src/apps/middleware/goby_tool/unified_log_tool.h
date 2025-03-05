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

#ifndef GOBY_APPS_MIDDLEWARE_GOBY_TOOL_UNIFIED_LOG_TOOL_H
#define GOBY_APPS_MIDDLEWARE_GOBY_TOOL_UNIFIED_LOG_TOOL_H

#include <boost/filesystem.hpp>

#include "goby/apps/middleware/goby_tool/log.pb.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/protobuf/log_convert_tool_config.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{

class UnifiedLogTool : public goby::middleware::Application<protobuf::UnifiedLogToolConfig>
{
  public:
    UnifiedLogTool();
    ~UnifiedLogTool() override {}

  private:
    void run() override { assert(false); }

  private:
};

class LogConvertToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::LogConvertToolConfig>
{
  public:
    LogConvertToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::LogConvertToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);

        if (cfg.input_file_size() == 0)
        {
            std::cerr << "No input file specified: use --help for command syntax" << std::endl;
            exit(EXIT_FAILURE);
        }

        if (cfg.has_output_file() && cfg.input_file_size() != 1)
        {
            std::cerr << "Only one input_file is allowed when explicitly setting output_file: use "
                         "--help for command syntax"
                      << std::endl;
            exit(EXIT_FAILURE);
        }

        // special case for backwards compatibility
        if (cfg.input_file_size() == 2)
        {
            boost::filesystem::path potential_output_path(cfg.input_file(1));
            if (potential_output_path.extension().native() != ".goby")
            {
                cfg.set_output_file(cfg.input_file(1));
                cfg.mutable_input_file()->RemoveLast();
            }
        }

        std::vector<std::string> input_files_or_dirs;
        for (const auto& input_path_str : cfg.input_file())
            input_files_or_dirs.push_back(input_path_str);
        cfg.clear_input_file();

        bool single_input_file = (input_files_or_dirs.size() == 1);

        auto add_file = [&cfg, &single_input_file](boost::filesystem::path input_path)
        {
            // ignore symlinks except for unitary case
            if (boost::filesystem::is_symlink(input_path))
            {
                if (single_input_file)
                    cfg.add_input_file(input_path.native());
                else
                    std::cerr << "Ignoring symlink in batch mode: " << input_path << std::endl;
            }
            else if (boost::filesystem::is_regular_file(input_path))
            {
                cfg.add_input_file(input_path.native());
            }
            else
            {
                std::cerr << "Ignoring non-regular file: " << input_path << std::endl;
            }
        };

        // check validity and convert directories into files
        for (const auto& input_path_str : input_files_or_dirs)
        {
            boost::filesystem::path input_path(input_path_str);
            if (!boost::filesystem::exists(input_path))
            {
                std::cerr << "Input file or directory does not exist: " << input_path_str
                          << std::endl;
                exit(EXIT_FAILURE);
            }

            if (boost::filesystem::is_directory(input_path))
            {
                single_input_file = false;
                for (auto const& entry : boost::filesystem::directory_iterator(input_path))
                {
                    if (entry.path().extension() == ".goby")
                        add_file(entry.path());
                }
            }
            else
            {
                add_file(input_path);
            }
        }
    }
};

class LogConvertTool : public goby::middleware::Application<protobuf::LogConvertToolConfig>,
                       public goby::middleware::ToolSharedLibraryLoader
{
  public:
    LogConvertTool();
    ~LogConvertTool() override {}

  private:
    void run() override { assert(false); }
};

} // namespace middleware
} // namespace apps
} // namespace goby

#endif
