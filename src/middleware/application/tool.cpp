// Copyright 2024:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Not Committed Yet
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <unistd.h>

#include <dccl/dynamic_protobuf_manager.h>

#include "tool.h"

bool goby::middleware::ToolHelper::help(int* action_for_help)
{
    std::string action_for_help_name =
        tool_cfg_.extra_cli_param_size() > 0
            ? tool_cfg_.extra_cli_param(tool_cfg_.extra_cli_param_size() - 1)
            : "";

    const google::protobuf::EnumValueDescriptor* help_action_value_desc =
        tool_cfg_.extra_cli_param_size() > 0
            ? action_enum_desc_->FindValueByName(action_for_help_name)
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

            exec_external(external_command, args, ev_options);

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
        if (!action_for_help_name.empty())
            std::cerr << "Action \"" << action_for_help_name << "\" does not exist.\n" << std::endl;

        std::cerr << "Usage: " << name_ << " [" << name_ << " options (use -h[hhh])] "
                  << goby::util::esc_lt_white << "action" << goby::util::esc_nocolor
                  << " [action options]\n"
                  << std::endl;

        std::cerr << "Available actions: " << std::endl;
        for (int i = 0; i < action_enum_desc_->value_count(); ++i)
        {
            const google::protobuf::EnumValueDescriptor* value_desc = action_enum_desc_->value(i);
            const goby::GobyEnumValueOptions& ev_options =
                value_desc->options().GetExtension(goby::ev);
            std::cerr << "  " << goby::util::esc_lt_white << value_desc->name()
                      << goby::util::esc_nocolor << ": " << ev_options.cfg().short_help_msg();

            if (ev_options.cfg().has_external_command())
                std::cerr << " [" << goby::util::esc_blue << ev_options.cfg().external_command()
                          << goby::util::esc_nocolor << "]";

            std::cerr << std::endl;
        }
        return true;
    }
}

void goby::middleware::ToolHelper::exec_external(std::string app, std::vector<std::string> args,
                                                 const goby::GobyEnumValueOptions& ev_options)
{
    std::vector<std::string> bin_args;
    if (ev_options.cfg().include_binary_flag())
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

void goby::middleware::ToolSharedLibraryLoader::load_lib(const std::string& lib)
{
    std::vector<std::string> libs;
    // allow the environmental variable entry to contain multiple libraries separated by a common delimiter
    boost::split(libs, lib, boost::is_any_of(";:,"));
    for (const auto& l : libs)
    {
        glog.is_debug2() && glog << "Loading library: " << l << std::endl;
        void* lib_handle = dlopen(l.c_str(), RTLD_LAZY);
        if (!lib_handle)
            glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
        dl_handles_.push_back(lib_handle);
    }
}

goby::middleware::ToolSharedLibraryLoader::~ToolSharedLibraryLoader()
{
    for (void* handle : dl_handles_) dlclose(handle);
}
