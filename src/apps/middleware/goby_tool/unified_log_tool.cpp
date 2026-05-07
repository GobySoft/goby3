// Copyright 2024-2025:
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

#include <boost/process.hpp>
#if __has_include(<boost/process/v1.hpp>)
#include <boost/process/v1.hpp>
#endif
#include <thread>

#include "goby/middleware/application/tool.h"

#include "unified_log_tool.h"

#if __has_include(<boost/process/v1.hpp>)
namespace bp = boost::process::v1;
#else
namespace bp = boost::process;
#endif

goby::apps::middleware::UnifiedLogTool::UnifiedLogTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::middleware::protobuf::UnifiedLogToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::middleware::protobuf::UnifiedLogToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::middleware::protobuf::UnifiedLogToolConfig::convert:
                            tool_helper.help<goby::apps::middleware::LogConvertTool,
                                             goby::apps::middleware::LogConvertToolConfigurator>(
                                action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::middleware::protobuf::UnifiedLogToolConfig::convert:
                tool_helper.run_subtool<goby::apps::middleware::LogConvertTool,
                                        goby::apps::middleware::LogConvertToolConfigurator>();
                break;

            default:
                throw(goby::Exception("Action was expected to be handled by external tool"));
                break;
        }
    }

    quit(0);
}

goby::apps::middleware::LogConvertTool::LogConvertTool()
    : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())
{
    std::deque<std::string> input_files;
    for (int i = 0, n = app_cfg().input_file_size(); i < n; ++i)
        input_files.push_back(app_cfg().input_file(i));

    std::list<bp::child> children;

    auto start_job = [this, &children](std::string input_file)
    {
        bp::opstream in;
        std::string in_str;
        auto child_cfg = app_cfg();
        child_cfg.clear_input_file();
        child_cfg.add_input_file(input_file);
        google::protobuf::TextFormat::PrintToString(child_cfg, &in_str);
        std::string name = input_file;
        children.emplace_back("goby_log_tool --app_name=" + name +
                                  " --binary=\"goby log convert\" -c -",
                              bp::std_in < in);
        in << in_str;
    };

    for (int j = 0, n = app_cfg().jobs(); j < n; ++j)
    {
        if (input_files.empty())
            break;

        start_job(input_files.front());
        input_files.pop_front();
    }

    while (!children.empty())
    {
        for (auto it = children.begin(); it != children.end();)
        {
            if (!it->running())
            {
                it = children.erase(it);
                if (!input_files.empty())
                {
                    start_job(input_files.front());
                    input_files.pop_front();
                }
            }
            else
            {
                ++it;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    quit(0);
}
