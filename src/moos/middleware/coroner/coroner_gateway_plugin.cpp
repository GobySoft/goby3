#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/coroner/coroner.h"

#include "goby/moos/middleware/moos_plugin_translator.h"
#include "goby/moos/moos_translator.h"

using goby::glog;

namespace goby
{
namespace moos
{
class CoronerTranslation : public goby::moos::Translator
{
  public:
    CoronerTranslation(const goby::apps::moos::protobuf::GobyMOOSGatewayConfig& cfg)
        : goby::moos::Translator(cfg)
    {
        goby()
            .interprocess()
            .subscribe<goby::middleware::groups::health_report,
                       goby::middleware::protobuf::VehicleHealth>(
                [this](const goby::middleware::protobuf::VehicleHealth& health) {
                    glog.is_debug2() && glog << "To MOOS: " << health.ShortDebugString()
                                             << std::endl;

                    std::string serialized;
                    bool is_binary = serialize_for_moos(&serialized, health);
                    CMOOSMsg moos_msg = goby::moos::MOOSTranslator::make_moos_msg(
                        "GOBY_PROCESS_HEALTH", serialized, is_binary, goby::moos::moos_technique,
                        health.GetDescriptor()->full_name());
                    moos().comms().Post(moos_msg);
                });
    }

  private:
    goby::moos::MOOSTranslator translator_;
};
} // namespace moos
} // namespace goby

extern "C"
{
    void goby3_moos_gateway_load(
        goby::zeromq::MultiThreadApplication<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>*
            handler)
    {
        handler->launch_thread<goby::moos::CoronerTranslation>();
    }

    void goby3_moos_gateway_unload(
        goby::zeromq::MultiThreadApplication<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>*
            handler)
    {
        handler->join_thread<goby::moos::CoronerTranslation>();
    }
}
