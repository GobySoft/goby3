#include <unistd.h>

#include <dccl/dynamic_protobuf_manager.h>

#include "tool.h"

bool goby::middleware::ToolHelper::help(int* action_for_help)
{
    const google::protobuf::EnumValueDescriptor* help_action_value_desc =
        tool_cfg_.extra_cli_param_size() > 0
            ? action_enum_desc_->FindValueByName(
                  tool_cfg_.extra_cli_param(tool_cfg_.extra_cli_param_size() - 1))
            : nullptr;
    if (help_action_value_desc != nullptr)
    {
        const goby::GobyEnumValueOptions& ev_options =
            help_action_value_desc->options().GetExtension(goby::ev);

        std::cerr << "Help for action: " << help_action_value_desc->name() << "\n" << std::endl;
        *action_for_help = help_action_value_desc->number();

        if (ev_options.cfg().has_full_help_msg())
        {
            std::cerr << ev_options.cfg().full_help_msg() << std::endl;
            return true;
        }
        else if (ev_options.cfg().has_external_command())
        {
            action_ = help_action_value_desc->name();

            // try external_command -h
            const std::string& external_command = ev_options.cfg().external_command();
            std::vector<std::string> args;

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

            exec_external(external_command, args);

            // should not reach here if exec_external worked
            return false;
        }
        else
        {
            return false;
        }
    }
    else
    {
        std::cerr << "Usage: " << name_ << " [" << name_
                  << " options (use -h[hhh])] action [action options]\n"
                  << std::endl;

        std::cerr << "Available actions: " << std::endl;
        for (int i = 0; i < action_enum_desc_->value_count(); ++i)
        {
            const google::protobuf::EnumValueDescriptor* value_desc = action_enum_desc_->value(i);
            const goby::GobyEnumValueOptions& ev_options =
                value_desc->options().GetExtension(goby::ev);
            std::cerr << "\t" << value_desc->name() << ": " << ev_options.cfg().short_help_msg()
                      << std::endl;
        }
        return true;
    }
}

void goby::middleware::ToolHelper::exec_external(std::string app, std::vector<std::string> args)
{
    std::vector<std::string> bin_args;
    bin_args.push_back("--binary=" + name_ + " " + action_);

    std::vector<char*> c_args;
    c_args.push_back(const_cast<char*>(app.c_str()));
    for (const auto& arg : bin_args) { c_args.push_back(const_cast<char*>(arg.c_str())); }
    for (const auto& arg : args) { c_args.push_back(const_cast<char*>(arg.c_str())); }
    c_args.push_back(nullptr); // execvp expects a null-terminated array

    execvp(c_args[0], c_args.data());
    // If execvp returns, there was an error
    std::cerr << "ERROR executing:\n\t";
    for (const auto& arg : c_args)
    {
        if (arg)
            std::cerr << "\"" << arg << "\" ";
    }
    std::cerr << std::endl;
    std::cerr << "Ensure that " << args[0] << " is on your path and is executable." << std::endl;
}
