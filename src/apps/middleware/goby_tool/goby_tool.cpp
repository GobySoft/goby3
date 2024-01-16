#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/middleware/protobuf/goby_tool_config.pb.h"

#include "unified_log_tool.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace middleware
{
class GobyTool : public goby::middleware::Application<protobuf::GobyToolConfig>
{
  public:
    GobyTool();
    ~GobyTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

} // namespace middleware
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::middleware::GobyTool>(argc, argv); }

goby::apps::middleware::GobyTool::GobyTool()
{
    //    std::cout << app_cfg().DebugString() << std::endl;

    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::middleware::protobuf::GobyToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::middleware::protobuf::GobyToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::middleware::protobuf::GobyToolConfig::log:
                            tool_helper.help<goby::apps::middleware::UnifiedLogTool>(
                                action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::middleware::protobuf::GobyToolConfig::log:
                tool_helper.run_subtool<goby::apps::middleware::UnifiedLogTool>();
                break;

            default:
                // perform action will call 'exec' if an external tool performs the action,
                // so if we are continuing, this didn't happen
                throw(goby::Exception("Action was expected to be handled by external tool"));
                break;
        }
    }

    quit(0);
}
