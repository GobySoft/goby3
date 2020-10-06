
#ifndef TransportInterModuleZeroMQ20201006H
#define TransportInterModuleZeroMQ20201006H

#include "goby/middleware/transport/intermodule.h"

#include "goby/zeromq/transport/interprocess.h"

namespace goby
{
namespace zeromq
{
template <typename InnerTransporter = middleware::NullTransporter>
using InterModulePortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterModulePortalBase>;
}
} // namespace goby

#endif
