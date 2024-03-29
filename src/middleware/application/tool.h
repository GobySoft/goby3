// Copyright 2024:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_TOOL_H
#define GOBY_MIDDLEWARE_APPLICATION_TOOL_H

#include <google/protobuf/descriptor.h>

#include "goby/middleware/application/configurator.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/protobuf/app_config.pb.h"
#include "goby/protobuf/option_extensions.pb.h"

namespace goby
{
namespace middleware
{
class ToolHelper
{
  public:
    ToolHelper(const std::string& name, const goby::middleware::protobuf::AppConfig::Tool& tool_cfg,
               const google::protobuf::EnumDescriptor* action_enum_desc)
        : name_(name), tool_cfg_(tool_cfg), action_enum_desc_(action_enum_desc)
    {
    }

    // return true if handled by this function, false otherwise
    template <typename Action> bool perform_action(Action action);

    // return true if handled by this function, false otherwise
    bool help(int* action_for_help);

    template <typename App,
              typename Configurator = middleware::ProtobufConfigurator<typename App::ConfigType>>
    void run_subtool();

    template <typename App,
              typename Configurator = middleware::ProtobufConfigurator<typename App::ConfigType>>
    void help(int action_for_help);

  private:
    void exec_external(std::string app, std::vector<std::string> args,
                       const goby::GobyEnumValueOptions& ev_options);
    template <typename App, typename Configurator>
    void exec_internal(std::string app, std::vector<std::string> args);

  private:
    std::string name_;
    const goby::middleware::protobuf::AppConfig::Tool& tool_cfg_;
    std::string action_;
    const google::protobuf::EnumDescriptor* action_enum_desc_;
};

class ToolSharedLibraryLoader
{
  public:
    ToolSharedLibraryLoader(const std::vector<std::string>& load_libs)
    {
        for (const auto& lib : load_libs) load_lib(lib);
    }

    ToolSharedLibraryLoader(const google::protobuf::RepeatedPtrField<std::string>& load_libs)
    {
        for (const auto& lib : load_libs) load_lib(lib);
    }

    ToolSharedLibraryLoader(const std::string& lib) { load_lib(lib); }
    ~ToolSharedLibraryLoader();

  private:
    void load_lib(const std::string& lib);

  private:
    std::vector<void*> dl_handles_;
};

} // namespace middleware
} // namespace goby

template <typename Action> bool goby::middleware::ToolHelper::perform_action(Action action)
{
    const google::protobuf::EnumValueDescriptor* value_desc =
        action_enum_desc_->FindValueByNumber(action);
    // Get the extended option for this enum value
    const goby::GobyEnumValueOptions& ev_options = value_desc->options().GetExtension(goby::ev);
    action_ = value_desc->name();

    if (ev_options.cfg().has_external_command())
    {
        const std::string& external_command = ev_options.cfg().external_command();
        std::vector<std::string> args;
        for (const auto& cli_extra : tool_cfg_.extra_cli_param()) args.push_back(cli_extra);
        exec_external(external_command, args, ev_options);
    }
    return false;
}

template <typename App, typename Configurator>
void goby::middleware::ToolHelper::exec_internal(std::string app, std::vector<std::string> args)
{
    std::vector<std::string> bin_args;
    bin_args.push_back("--binary=" + name_ + " " + action_);

    std::vector<char*> c_args;
    c_args.push_back(const_cast<char*>(app.c_str()));
    for (const auto& arg : bin_args) { c_args.push_back(const_cast<char*>(arg.c_str())); }
    for (const auto& arg : args) { c_args.push_back(const_cast<char*>(arg.c_str())); }
    goby::run<App>(Configurator(c_args.size(), c_args.data()));
}

template <typename App, typename Configurator> void goby::middleware::ToolHelper::run_subtool()

{
    std::vector<std::string> args;
    for (const auto& cli_extra : tool_cfg_.extra_cli_param()) args.push_back(cli_extra);
    exec_internal<App, Configurator>(name_, args);
}

template <typename App, typename Configurator>
void goby::middleware::ToolHelper::help(int action_for_help)

{
    const google::protobuf::EnumValueDescriptor* help_action_value_desc =
        action_enum_desc_->FindValueByNumber(action_for_help);

    const goby::GobyEnumValueOptions& ev_options =
        help_action_value_desc->options().GetExtension(goby::ev);

    action_ = help_action_value_desc->name();

    std::vector<std::string> args;
    if (tool_cfg_.extra_cli_param_size() > 1)
    {
        for (int i = 0, n = tool_cfg_.extra_cli_param_size() - 1; i < n; ++i)
            args.push_back(tool_cfg_.extra_cli_param(i));
    }
    else
    {
        args.push_back(ev_options.cfg().external_help_param());
    }

    exec_internal<App, Configurator>(name_, args);
}

#endif
