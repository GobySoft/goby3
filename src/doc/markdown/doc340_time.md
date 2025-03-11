# goby-time: Time and date functionality

## Overview

The goby-time part of the goby library is designed for two main goals:

- Enable faster-than-realtime simulations
- Allow conversions between various time and date representations that are useful in different contexts:
	+ `std::chrono`: The C++11 (and newer) time library. Until C++20, `std::chrono` did not provide date or calendar functions. 
	+ `boost::posix_time::ptime`: Part of the [Boost Date-Time library](https://www.boost.org/doc/libs/release/doc/html/date_time/posix_time.html), a widely used date-time library prior to C++20's date functions in std::chrono.
	+ `boost::units::quantity<`*time_dimension*`>`: Durations and timestamps (typically UNIX) using the [Boost::Units library](https://www.boost.org/doc/libs/release/doc/html/boost_units.html). The meshes with [DCCL's](https://libdccl.org/4.0/index.html) unit safety feature.
	
Several convenience types are defined (`#include <goby/time/types.h>`) for writing more concise code:
- `goby::time::MicroTime`: *int64* microseconds since Unix Epoch (Jan 1, 1970 midnight UTC). Defined as a boost::units::quantity.
- `goby::time::SITime`: *double* seconds since Unix Epoch. Defined as a boost::units::quantity.
	
	
## Steady Clock

```
#include <goby/time/steady_clock.h>
```

The goby::time::SteadyClock is a thin wrapper around std::chrono::steady_clock that allows for faster than real time simulations using the SimulatorSettings struct. You can use it just like std::chrono::steady_clock in most contexts.

To enable faster than real time results from goby::time::SteadyClock, you can set the global variables:

```
#include <goby/time/simulation.h>

goby::time::SimulatorSettings::using_sim_time = true;
goby::time::SimulatorSettings::warp_factor = 10; // 10x real clock speed
```

If you're using a Goby Application, you can set this values in the configuration file with:

```
app { 
  simulation {
    using_sim_time: true
    warp_factor: 10
  }
}
```

## System Clock

```
#include <goby/time/system_clock.h>
```

Like goby::time::SteadyClock, goby::time::SystemClock is a wrapper around std::chrono::system_clock to provide faster than real time simulated "wall" times. 

Similarly to SteadyClock, you enable faster than real time with the goby::time::SimulatorSettings struct:

```
#include <goby/time/simulation.h>

goby::time::SimulatorSettings::using_sim_time = true;
goby::time::SimulatorSettings::warp_factor = 10; // 10x real clock speed
# optional starting reference for the faster than real time clock
# if omitted, the clock begins on midnight UTC of Jan 1 of the current year and runs at "warp" times real speed from then
goby::time::SimulatorSettings::reference_time = std::chrono::system_clock::epoch;
```

For Goby Applications use the equivalent configuration file settings:

```
app { 
  simulation {
    using_sim_time: true
    warp_factor: 10
    # microseconds since UNIX epoch to use as reference time
    # optional, defaults to Jan 1 of the current year midnight UTC
    reference_microtime: 1741716274000000
  }
}
```


## Use in Goby Applications

To ensure compatibility with the faster than real time "warp" feature, you should always use either goby::time::SteadyClock (preferred, when wall time isn't required) or goby::time::SystemClock in your Applications. If you don't, your applications won't generally work correctly in "warp" simulations.

## I/O

```
#include <goby/time/io.h>
```

This header includes `operator<<` overloads for goby::time::SystemClock::time_point, goby::time::SteadyClock::time_point, and goby::time::SystemClock::duration for logging or debugging convenience.

## Conversions

```
#include <goby/time/convert.h>
```

This header includes a family of templated functions for converting wall times to/and from dates and durations in different representations:

- `ToTimeType goby::time::convert(FromTimeType from_time)`: absolute wall time conversion
	- two different boost::units::quantity of time types (for example: goby::time::MicroTime to/from goby::time::SITime).
	- boost::units::quantity of time to/from std::chrono::system_clock::time_point or goby::time::SystemClock::time_point.
	- any time type previously mentioned to/from boost::posix_time::ptime
* `ToDurationType convert_duration(FromDurationType from_duration)`: duration conversion
	- two different boost::units::quantity of time types
	- std::chrono::duration to/from boost::units::quantity of time
* `ToTimeType convert_from_nmea`: For converting NMEA0183 timestamps.
* `TimeType goby::time::SystemClock::now()`: Convenience function for calling goby::time::convert on the return of goby::time::SystemClock::now.
* `std::string str(TimeType value)`: Human readable string version of the given time (for debugging, logging, etc.), e.g., "2025-Mar-11 18:22:49.880977"
* `std::string file_str(TimeType value)`: Representation of time formatted for a file name without special characters, e.g., "20250311T182250"

When using boost::units::quantity of time, it's important to correctly choose between `convert` or `convert_duration` function as this representation is ambiguous as to whether it refers to a duration or a wall time (referenced to the UNIX Epoch).