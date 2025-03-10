# goby-zeromq: ZeroMQ Applications

Goby comes with a number of useful applications for use with the ZeroMQ implementation of the Goby3 middleware interprocess layer. These applications are summarized here, and those that require more detail have dedicated documentation pages.

This written documentation is only a high level overview and reference. For a more comprehensive introduction, it is recommended that you watch and participate in the Goby3 Course materials: https://gobysoft.org/training/goby3-free-course.

For additional applications that may be useful for those running goby-zeromq, but are not ZeroMQ-specific, please see the [Middleware Appplications](doc201_middleware_applications.md) page.

## Common configuration

All goby-zeromq applications can take parameters from either the command line or a configuration file or both (where command line takes precedence if set in both places).

To assist the user, these applications all have built-in assistance for these configuration file or command line parameters using:

```
# Show configuration file syntax
goby_logger -e
# Show command line parameters
goby_logger -h
```

Increasingly advanced options for the configuration file are shown when specifying `-ee`, `-eee`, or `-eeee` or equivalently with `-hh`, `-hhh`, and `-hhhh` for command line parameters. When possible, you should prefer the built-in assistance to this documentation, as it is always current whereas this document may not be.

Goby-zeromq applications all have two configuration blocks that are the same: the `app` block (used to configure common aspects of all Goby applications) and the `interprocess` block (used to configure the ZeroMQ interprocess connection).

### app block

The full set of options for the app block can (and should) be retrieved as explained in the previous section using `-e` or `-h`. Here we will quick outline the most important aspects:

```
app {  #  (optional)
  glog_config { ... }
  simulation { ... }
}
```

The `glog_config` block affects the output of `glog`, which is the Goby version of a stream logger (like std::cout). `glog` can be configured to output to the terminal (std::cout), to a file, both, or neither. The verbosity setting dictates which messages are displayed, where QUIET is no messages, and DEBUG3 is all messages.

The `simulation` block allows you to run faster than realtime simulations when using the `goby::time` functions. By setting `use_sim_time: true` and `warp_factor: N` where N is greater than 1, your simulations will run at N times real speed. This requires that all applications use `goby::time` rather than `std::chrono` directly.

### interprocess block

The `interprocess` block determines how to connect to `gobyd`. By specifying parameters in this block you can run multiple separate systems on a single machine. The `interprocess` block will typically be identical for all applications (including `gobyd`) that should be in the same `interprocess` publish/subscribe world.

A simple configuration (using ZeroMQ IPC, aka UNIX sockets) would look like:
```
interprocess { platform: "auv1" }
```

If you want to spread your applications across a local network you could use ZeroMQ TCP instead:

```
interprocess {
  platform: "auv1" 
  transport: TCP
  ip_address: "192.168.0.5" # where gobyd is running 
  tcp_port: 11144 # Manager listen port for gobyd 
}
```

Make sure to use unique `tcp_port` settings for each `gobyd` you want to run in a single machine.

## gobyd

gobyd provides two functions:

1. It runs a goby::zeromq::Router and goby::zeromq::Manager for mediating the ZeroMQ-based interprocess comms between all other apps (broker).
2. It can optionally run an intervehicle portal for connecting to various intervehicle modems and links to provide intervehicle layer communications for a particular node. Alternatively, this functionality can be run as a separate app using `goby_intervehicle_portal`.

## goby_intervehicle_portal

This application provides identical functionality to `gobyd`'s intervehicle portal. This is provided as a separate app for users who wish to keep the intervehicle comms (and associated drivers) separate from the ZeroMQ broker responsibilities for `gobyd`. This is especially helpful if the various drivers are less stable than the rest of the codebase, since if `gobyd` crashes (e.g., due to a faulty driver) it stops all interprocess comms.

## goby_terminate

`goby_terminate` is a tool used to cleanly shut down Goby applications via a publication. It is used by `goby_launch` by  default and can manually be called using 

```
goby_terminate --interprocess "..." --target_name "app_name"
```
where the `--interprocess` settings match that of the desired `gobyd` and "app_name" is the application name. Alternatively, `--target_pid` can be used to terminate the desired process ID (PID) rather than specifying a name.

## goby_gps

`goby_gps` is a client for [gpsd](https://gpsd.gitlab.io/gpsd/index.html) that publishes the GPS data from gpsd on several ZeroMQ interprocess groups (thus allowing Goby subscribers to access GPS data readily). `goby_gps` does not directly connect to the NMEA-0183 stream from the GPS device, as by using `gpsd` you open up the use of other useful clients, especially time-keeping (e.g., NTP).

See the [goby_gps](doc502_goby_gps.md) page for more details.

## goby_coroner

`goby_coroner` is a application that requests health status of one or more goby applications and creates an aggregate report. This is useful for monitoring if a process has died or hangs.

See the [goby_coroner](doc503_goby_coroner.md) page for more details.

## goby_logger

`goby_logger` writes all or some of the *interprocess* publications to a log file (.goby) for later analysis or playback. This file is a flat binary file designed for fast writing, and can be converted to more useful formats such as HDF5, JSON or text using the `goby log convert` function.

See the [goby_logger](doc504_goby_logger.md) page for more details.

## goby_playback

The `goby_playback` tool allows parts of a previously collected log file (.goby, written by `goby_logger`) to be replayed into a running gobyd platform. This is useful for post mission analysis, re-processing mission data with new or modified tools, or  simulations that use partial field collected data.

See the [goby_logger](doc504_goby_logger.md) page (which also covers goby_playback) for more details.

## goby_liaison

`goby_liaison` is an HTTP server for an extensible web application that includes two useful built-in tabs:

1. Commander: for publishing any Protobuf message using a web form.
2. Scope: for visualizing all Protobuf messages on the interprocess layer.

See the [goby_liaison](doc505_goby_liaison.md) page for more details.

## goby_frontseat_interface

The `goby_frontseat_interface` is an extensible interface between Goby (which can be thought of as a "backseat driver") to a frontseat or vehicle computer (often a proprietary low level control computer). This is the Goby version of the [MOOS `iFrontSeat`](doc600_moos.md). The iFrontSeat document explains how to write a new driver, which is identical for writing a driver for `goby_frontseat_interface` (they can share the same drivers).

See the [goby_frontseat_interface](doc506_goby_frontseat_interface.md) page for more details.

## goby_geov_interface

The `goby_geov_interface` provides an interface between Goby3 and the [Google Earth interface for Ocean Vehicles](https://gobysoft.org/geov/) (GEOV, pronounced "jove"). This allows you to visualize vehicles from Goby3 using the 3D rendering of [Google Earth for Desktop](https://www.google.com/earth/about/versions/#download-pro).

See the [Goby Visualization Interfaces](doc507_goby_viz.md) page for more details.

## goby_opencpn_interface

[OpenCPN](https://www.opencpn.org/) is an open source Chart Plotter Navigation software that can be used to display (2D) positions of marine vehicles. The `goby_opencpn_interface` provides a way to feed OpenCPN positions of autonomous vehicles by mimicing an AIS receiver.

See the [Goby Visualization Interfaces](doc507_goby_viz.md) page for more details.

## goby_mavlink_gateway

A gateway application between MAVLink (initially developed for use by unmanned aerial vehicles) and Goby3.

You can connect to MAVLink by either serial or UDP.

## Alternative (a)comms applications

The following are alternatives to using the goby `intervehicle` layer that will give a more traditional (Goby2-style) interface to the goby-acomms slow link module. This is mostly for older applications that pre-date the goby `intervehicle` comms layer, but may occasionally be useful for new systems that want more fine grain control over parts of the goby-acomms libraries.

### goby_modemdriver

Connects to a modem using the Goby-Modemdriver infrastructure. Data are passed to and from the modem using *interprocess* messages:



### goby_bridge

Runs the rest of the Goby-Acomms stack besides the ModemDriver: AMAC, Queue, and Route. This has two ends, each connecting to a different `goby_modemdriver`. This is, as the name suggests, intended to bridge messages between two different media (e.g., acoustic modem to satcomms, satcomms to store-and-forward, etc.)

### goby_mosh_relay 

Interfaces with a [forked version of Mosh](https://github.com/tsaubergine/mosh/tree/mosh-goby) to provide slow but usable remote shell connections using `goby_modemdriver`.

### goby_file_transfer

Splits files into components to be sent using `goby_modemdriver`, sends them, and reconstructs the file on the receiving end. 

Groups are defined in 

```
#include "goby/middleware/acomms/groups.h"
```

#### Subscriptions

- Dynamic Group: String **goby::middleware::acomms::groups::tx**, Numeric **modem_id** (as defined in configuration). Message *goby::acomms::protobuf::ModemTransmission*. Messages to transmit via modem.
- Dynamic Group: String **goby::middleware::acomms::groups::data_response**, Numeric **modem_id** (as defined in configuration). Response to **data_request**.


#### Publications

- Dynamic Group: String **goby::middleware::acomms::groups::rx**, Numeric **modem_id** (as defined in configuration). Message *goby::acomms::protobuf::ModemTransmission*. Messages received from modem.
- Dynamic Group: String **goby::middleware::acomms::groups::data_request**, Numeric **modem_id** (as defined in configuration). Messages requesting data (from the application communicating with `goby_modemdriver`).


### goby_ip_gateway

Interfaces Goby with a UDP/IP tunnel using a minimal rewritten IP header. See https://ieeexplore.ieee.org/abstract/document/7778678:

```
T. Schneider, "Transmitting Internet Protocol packets efficiently on underwater networks using entropy-encoder header translation," 2016 IEEE/OES Autonomous Underwater Vehicles (AUV), Tokyo, Japan, 2016, pp. 241-245, doi: 10.1109/AUV.2016.7778678.
```
