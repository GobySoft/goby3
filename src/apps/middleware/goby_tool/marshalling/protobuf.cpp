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

#include <dccl/dynamic_protobuf_manager.h>

#include "goby/middleware/application/tool.h"

#include "protobuf.h"

using goby::glog;

goby::apps::middleware::ProtobufTool::ProtobufTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::middleware::protobuf::ProtobufToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::middleware::protobuf::ProtobufToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::middleware::protobuf::ProtobufToolConfig::show:
                            tool_helper.help<goby::apps::middleware::ProtobufShowTool>(
                                action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::middleware::protobuf::ProtobufToolConfig::show:
                tool_helper.run_subtool<goby::apps::middleware::ProtobufShowTool>();
                break;

            default:
                throw(goby::Exception("Action was expected to be handled by external tool"));
                break;
        }
    }

    quit(0);
}

goby::apps::middleware::ProtobufShowTool::ProtobufShowTool()
    : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())
{
    for (auto name : app_cfg().name())
    {
        if (app_cfg().has_package_name())
            name = app_cfg().package_name() + "." + name;
        const google::protobuf::Descriptor* desc =
            dccl::DynamicProtobufManager::find_descriptor(name);

        if (!desc)
        {
            goby::glog.is_die() &&
                goby::glog << "Failed to find message " << name
                           << ". Ensure you have specified all required --load_shared_library "
                              "libraries and set --package_name (if any)"
                           << std::endl;
        }

        std::cout << "============== " << goby::util::esc_lt_white << name
                  << goby::util::esc_nocolor << " ==============" << std::endl;
        std::cout << desc->DebugString() << std::endl;
    }

    quit(0);
}
