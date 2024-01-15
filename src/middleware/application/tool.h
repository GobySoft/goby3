#ifndef GOBY_MIDDLEWARE_APPLICATION_TOOL_H
#define GOBY_MIDDLEWARE_APPLICATION_TOOL_H

#include <google/protobuf/descriptor.h>

#include "goby/middleware/application/configurator.h"
#include "goby/middleware/protobuf/app_config.pb.h"
#include "goby/protobuf/option_extensions.pb.h"

namespace goby
{
namespace middleware
{
class ToolHelper
{
  public:
    ToolHelper(const std::string& name, const goby::middleware::protobuf::AppConfig::Tool& tool_cfg)
        : name_(name), tool_cfg_(tool_cfg)
    {
    }

    template <typename Action>
    void perform_action(Action action, const google::protobuf::EnumDescriptor* action_enum_desc);

    void help(const google::protobuf::EnumDescriptor* action_enum_desc);

  private:
    void exec_external(std::vector<std::string> args);

  private:
    std::string name_;
    const goby::middleware::protobuf::AppConfig::Tool& tool_cfg_;
    std::string action_;
};

} // namespace middleware
} // namespace goby

template <typename Action>
void goby::middleware::ToolHelper::perform_action(
    Action action, const google::protobuf::EnumDescriptor* action_enum_desc)
{
    const google::protobuf::EnumValueDescriptor* value_desc =
        action_enum_desc->FindValueByNumber(action);
    // Get the extended option for this enum value
    const goby::GobyEnumValueOptions& ev_options = value_desc->options().GetExtension(goby::ev);
    action_ = value_desc->name();

    if (ev_options.cfg().has_external_command())
    {
        const std::string& external_command = ev_options.cfg().external_command();
        std::vector<std::string> args{external_command};
        for (const auto& cli_extra : tool_cfg_.extra_cli_param()) args.push_back(cli_extra);
        exec_external(args);
    }
}

#endif
