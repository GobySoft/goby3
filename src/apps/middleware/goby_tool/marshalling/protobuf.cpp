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
{
    for (const auto& lib : app_cfg().load_shared_library())
    {
        void* lib_handle = dlopen(lib.c_str(), RTLD_LAZY);
        if (!lib_handle)
            glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
        dl_handles_.push_back(lib_handle);
    }

    const google::protobuf::Descriptor* desc =
        dccl::DynamicProtobufManager::find_descriptor(app_cfg().name());

    if (!desc)
    {
        goby::glog.is_die() &&
            goby::glog << "Failed to find message " << app_cfg().name()
                       << ". Ensure you have specified all required --load_shared_library libraries"
                       << std::endl;
    }

    std::cout << desc->DebugString() << std::endl;

    quit(0);
}
