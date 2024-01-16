#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/tool_config.pb.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class ZeroMQToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ZeroMQToolConfig>
{
  public:
    ZeroMQToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ZeroMQToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ZeroMQTool : public goby::middleware::Application<protobuf::ZeroMQToolConfig>
{
  public:
    ZeroMQTool();
    ~ZeroMQTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

class PublishTool : public goby::zeromq::SingleThreadApplication<protobuf::PublishToolConfig>
{
  public:
    PublishTool() {}
    ~PublishTool() override {}

  private:
    void loop() override { assert(false); }

  private:
};

class SubscribeTool : public goby::zeromq::SingleThreadApplication<protobuf::SubscribeToolConfig>
{
  public:
    SubscribeTool() {}
    ~SubscribeTool() override {}

  private:
    void loop() override { assert(false); }

  private:
};

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::ZeroMQTool>(
        goby::apps::zeromq::ZeroMQToolConfigurator(argc, argv));
}

goby::apps::zeromq::ZeroMQTool::ZeroMQTool()
{
    //    std::cout << app_cfg().DebugString() << std::endl;

    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::zeromq::protobuf::ZeroMQToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::publish:
                            tool_helper.help<goby::apps::zeromq::PublishTool>(action_for_help);
                            break;

                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::subscribe:
                            tool_helper.help<goby::apps::zeromq::SubscribeTool>(action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::publish:
                tool_helper.run_subtool<goby::apps::zeromq::PublishTool>();
                break;

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::subscribe:
                tool_helper.run_subtool<goby::apps::zeromq::SubscribeTool>();
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
