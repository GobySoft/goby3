#include <unistd.h>

#include <dccl/dynamic_protobuf_manager.h>

#include "tool.h"

void goby::middleware::ToolHelper::help(const google::protobuf::EnumDescriptor* action_enum_desc)
{
    const google::protobuf::EnumValueDescriptor* help_action_value_desc =
        tool_cfg_.extra_cli_param_size() > 0
            ? action_enum_desc->FindValueByName(
                  tool_cfg_.extra_cli_param(tool_cfg_.extra_cli_param_size() - 1))
            : nullptr;
    if (help_action_value_desc != nullptr)
    {
        const goby::GobyEnumValueOptions& ev_options =
            help_action_value_desc->options().GetExtension(goby::ev);

        std::cerr << "Help for action: " << help_action_value_desc->name() << "\n" << std::endl;
        if (ev_options.cfg().has_full_help_msg())
        {
            std::cerr << ev_options.cfg().full_help_msg() << std::endl;
        }
        else if (ev_options.cfg().has_external_command())
        {
            action_ = help_action_value_desc->name();

            // try external_command -h
            const std::string& external_command = ev_options.cfg().external_command();
            std::vector<std::string> args{external_command};

            if (tool_cfg_.extra_cli_param_size() > 1)
            {
                // allow commands like "goby help -hhh log" -> goby_log_tool -hhh
                for (int i = 0, n = tool_cfg_.extra_cli_param_size() - 1; i < n; ++i)
                    args.push_back(tool_cfg_.extra_cli_param(i));
            }
            else
            {
                args.push_back(ev_options.cfg().external_help_param());
            }

            exec_external(args);
        }
        else
        {
            std::cerr << "No help available [set (goby.ev).cfg.cfg_pb for auto-help on internal "
                         "tool actions] ..."
                      << std::endl;
        }
    }
    else
    {
        std::cerr << "Usage: " << name_ << " [" << name_
                  << " options (use -h[hhh])] action [action options]\n"
                  << std::endl;

        std::cerr << "Available actions: " << std::endl;
        for (int i = 0; i < action_enum_desc->value_count(); ++i)
        {
            const google::protobuf::EnumValueDescriptor* value_desc = action_enum_desc->value(i);
            const goby::GobyEnumValueOptions& ev_options =
                value_desc->options().GetExtension(goby::ev);
            std::cerr << "\t" << value_desc->name() << ": " << ev_options.cfg().short_help_msg()
                      << std::endl;
        }
    }
}

void goby::middleware::ToolHelper::exec_external(std::vector<std::string> args)
{
    args.push_back("--binary");
    args.push_back(name_ + " " + action_);

    std::vector<char*> c_args;
    for (const auto& arg : args) { c_args.push_back(const_cast<char*>(arg.c_str())); }
    c_args.push_back(nullptr); // execvp expects a null-terminated array

    execvp(c_args[0], c_args.data());
    // If execvp returns, there was an error
    std::cerr << "ERROR executing:\n\t";
    for (const auto& arg : args) std::cerr << arg << " ";
    std::cerr << std::endl;
    std::cerr << "Ensure that " << args[0] << " is on your path and is executable." << std::endl;
}
