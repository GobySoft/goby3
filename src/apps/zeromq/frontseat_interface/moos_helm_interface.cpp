#include "goby/moos/middleware/frontseat/frontseat_gateway_plugin.h"
#include "goby/moos/protobuf/moos_helm_frontseat_interface_config.pb.h"

#include "frontseat_interface.h"

void goby::apps::zeromq::FrontSeatInterface::launch_helm_interface()
{
    if (cfg().HasExtension(goby::moos::protobuf::moos_helm))
    {
        goby::glog.is_verbose() && goby::glog << "Launching MOOS Helm interface thread"
                                              << std::endl;

        goby::apps::moos::protobuf::GobyMOOSGatewayConfig gateway_config;
        *gateway_config.mutable_app() = cfg().app();
        *gateway_config.mutable_moos() = cfg().GetExtension(goby::moos::protobuf::moos_helm);
        launch_thread<goby::moos::FrontSeatTranslation>(gateway_config);
    }
    else
    {
        goby::glog.is_die() && goby::glog << "Missing [goby.moos.protobuf.moos_helm] config"
                                          << std::endl;
    }
}
