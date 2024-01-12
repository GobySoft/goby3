#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/protobuf/goby_tool_config.pb.h"

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
    void run() override {}

  private:
};
} // namespace middleware
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::middleware::GobyTool>(argc, argv); }

goby::apps::middleware::GobyTool::GobyTool()
{
    std::cout << app_cfg().DebugString() << std::endl;

    quit(0);
}
