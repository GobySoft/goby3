#pragma once

#include "goby/zeromq/protobuf/interprocess_config.pb.h"

namespace goby
{
namespace middleware
{
namespace detail
{
inline goby::zeromq::protobuf::InterProcessPortalConfig
make_interprocess_config(goby::zeromq::protobuf::InterProcessPortalConfig cfg, std::string app_name)
{
    cfg.set_client_name(app_name);
    return cfg;
}

} // namespace detail
} // namespace middleware
} // namespace goby
