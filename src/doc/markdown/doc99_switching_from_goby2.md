## Switching from Goby2

### General

- More consistent namespacing. This means some classes that didn't have a namespace previously now do (especially in `goby::moos`).
- Goby PB has been removed (this was a work in progress for what has now morphed into `libgoby_zeromq`).
- `libgoby_util`, `libgoby_acomms`, `libgoby_common` are all now in the new unified `libgoby` along with the new `middleware` code.
- `goby/common/protobuf/option_extensions.proto` moved to `goby/protobuf/option_extensions.proto`
- `goby::common::logger` is now `goby::util::logger` and `goby/common/logger.h` is `goby/util/debug_logger.h`
- util/seawater functions are now based on Boost Units and are in the namespace `goby::util::seawater`. Similarly for `goby::util::mackenzie_soundspeed` (now `goby::util::seawater::mackenzie_soundspeed`).
- `goby::common::Colors` is now `goby::util::Colors`

### Time

- Time has been re-implemented in light of std::chrono. Because std::chrono doesn't support dates in C++14, we still support converting to and from boost::posix_time::ptime for this purpose. In addition, Boost Units quantities of time are supported for compatibility with DCCL units (http://www.libdccl.org/idl.html). The old time functions are availabled (but deprecated) by `#include <goby/time/legacy.h>` (instead of `#include <goby/common/time.h>`)
- Use goby::time::SystemClock which is a simulation warpable version of std::chrono::system_clock for world-reference time.
- Use goby::time::SteadyClock which is a simulation warpable version of std::chrono::steady_clock for steady time tasks (when you don't want to deal with potential changes in the time due to NTP, etc.).

### MOOS

- `GobyMOOSAppConfig` is now  `goby.moos.protobuf.GobyMOOSAppConfig`
- `GobyMOOSApp` is now `goby::moos::GobyMOOSApp`
- `moos_gateway_g` is replaced by `goby_moos_gateway,` which provides runtime pluggable shared library translators as needed for a given application.
- pAcommsHandler configuration:
    - `driver_type` is now a child of `driver_cfg`


### Acomms

 - Use UDPMulticastDriver instead of PBDriver for pure software simulations
 - `micromodem.protobuf` moved to `goby.acomms.micromodem.protobuf`, and similar for other drivers