#ifndef GOBY_APPS_MIDDLEWARE_GOBY_TOOL_UNIFIED_LOG_TOOL_H
#define GOBY_APPS_MIDDLEWARE_GOBY_TOOL_UNIFIED_LOG_TOOL_H

#include "goby/middleware/application/interface.h"
#include "goby/middleware/protobuf/goby_tool_config.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{

class UnifiedLogTool : public goby::middleware::Application<protobuf::UnifiedLogToolConfig>
{
  public:
    UnifiedLogTool();
    ~UnifiedLogTool() override {}

  private:
    void run() override { assert(false); }

  private:
};

} // namespace middleware
} // namespace apps
} // namespace goby

#endif
