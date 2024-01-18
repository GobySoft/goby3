#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"

#include "goby/apps/middleware/goby_tool/marshalling/protobuf.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
class ProtobufToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ProtobufToolConfig>
{
  public:
    ProtobufToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ProtobufToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ProtobufTool : public goby::middleware::Application<protobuf::ProtobufToolConfig>
{
  public:
    ProtobufTool();
    ~ProtobufTool() override {}

  private:
    void run() override { assert(false); }

  private:
};

class ProtobufShowToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ProtobufShowToolConfig>
{
  public:
    ProtobufShowToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ProtobufShowToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ProtobufShowTool : public goby::middleware::Application<protobuf::ProtobufShowToolConfig>
{
  public:
    ProtobufShowTool();
    ~ProtobufShowTool() override
    {
        for (void* handle : dl_handles_) dlclose(handle);
    }

  private:
    void run() override { assert(false); }

  private:
    std::vector<void*> dl_handles_;
};

} // namespace middleware
} // namespace apps
} // namespace goby
