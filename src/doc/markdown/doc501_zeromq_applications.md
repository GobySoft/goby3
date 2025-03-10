# goby-zeromq: ZeroMQ Applications

Goby comes with a number of useful applications for use with the ZeroMQ implementation of the Goby3 middleware interprocess layer. These applications are summarized here, and those that require more detail have dedicated documentation pages.

This written documentation is only a high level overview and reference. For a more comprehensive introduction, it is recommended that you watch and participate in the Goby3 Course materials: https://gobysoft.org/training/goby3-free-course.


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

TODO: Document

## goby_geov_interface

TODO: Document

## goby_opencpn_interface

TODO: Document

## goby_terminate

TODO: Document

## goby_store_server

## Alternative (a)comms applications

The following are alternatives to using the goby `intervehicle` layer that will give a more traditional (Goby2-style) interface to the goby-acomms slow link module. This is mostly for older applications that pre-date the goby `intervehicle` comms layer, but may occasionally be useful for new systems that want more fine grain control over parts of the goby-acomms libraries.

### goby_modemdriver

TODO: Document

### goby_bridge

TODO: Document

### goby_mosh_relay 

TODO: Document

### goby_file_transfer

TODO: Document