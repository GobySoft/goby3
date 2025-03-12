# goby-cli: Command Line Tool

The `goby` command line tool is a starting point for managing, running, and understanding a Goby system from the Linux command line.

Like many modern tools (e.g., git) is has many sub-commands that allow you to access a wealth of functionality from a simple natural language syntax with built-in help.

The `goby` tool is available if you install from the `goby3-apps` package or build from source.

## goby help

The help command gives information on any subcommand. For example:

```
goby help zeromq
```

gives help on the zeromq command.

## goby log

This function is useful for post-mission analysis of .goby log files written by `goby_logger`.

### goby log convert

Converts a .goby log file into useful formats: HDF5, JSON, etc. See the [goby_logger](doc504_goby_logger.md) page for more details.

## goby launch

`goby launch` is a basic process manager. It launches a number of Goby application at once in series from a single `.launch` file. At its simplest, a .launch file is simply a list of commands to execute

```
gobyd
goby_logger --log_dir=/var/log/goby
goby_liaison
```

`goby launch` runs them in a [screen](https://www.gnu.org/software/screen/) session by default. You can type CTRL-C to quit, using `goby_terminate` by default.

See `goby help launch` for more options.

## goby zeromq

This function is for launching and interfacing with the ZeroMQ *interprocess* applications.

The following commands are shortcuts to the equivalent application described in the [ZeroMQ applications](doc501_zeromq_applications.md) page:

- `goby zeromq terminate`: runs `goby_terminate`
- `goby zeromq playback`: runs `goby_playback`
- `goby zeromq daemon`: runs `gobyd`
- `goby zeromq logger`: runs `goby_logger`
- `goby zeromq coroner`: runs `goby_coroner`
- `goby zeromq intervehicle_portal`: runs `goby_intervehicle_portal`
- `goby zeromq gps`: runs `goby_gps`
- `goby zeromq geov`: runs `goby_geov_interface`
- `goby zeromq frontseat_interface`: runs `goby_frontseat_interface`
- `goby zeromq liaison`: runs `goby_liaison`
- `goby zeromq opencpn`: runs `goby_opencpn_interface`
- `goby zeromq moos_gateway`: runs `goby_moos_gateway`

### goby zeromq publish

Publishes a single message to `gobyd` on the *interprocess* layer. You can either use the `--interprocess` parameter or set the environmental variable `GOBY_INTERPROCESS` to define the `gobyd` interprocess settings.

Usage:

```
goby zeromq publish <group> <type> <value> --interprocess "<interprocess settings>"
```

You can use `-l` to load shared libraries containing your custom Protobuf messages, or set the environmental variable GOBY_TOOL_LOAD_SHARED_LIBRARY.

### goby zeromq subscribe

Subscribes to all or some messages from `gobyd` on the *interprocess* layer. You can either use the `--interprocess` parameter or set the environmental variable `GOBY_INTERPROCESS` to define the `gobyd` interprocess settings.

```
goby zeromq subscribe [<group_regex>] [<type_regex>] [<scheme>] --interprocess "<interprocess settings>"
```


You can use `-l` to load shared libraries containing your custom Protobuf messages, or set the environmental variable GOBY_TOOL_LOAD_SHARED_LIBRARY.

If you omit the optional regex settings, they default to ".*", meaning that you'll see all publications.

### Example of goby zeromq publish/subscribe

(optional) Set the desired interprocess community in your .bashrc or similar:
```
export GOBY_INTERPROCESS='platform: "auv0"'
```

In one terminal:
```
gobyd
```

In another terminal:

```
goby zeromq subscribe
```

Finally, in a third terminal:

```
goby zeromq publish nav_group goby.middleware.protobuf.LatLonPoint 'lat: 42.5 lon: -70.3'
```

This publishes the goby::middleware::protobuf::LatLonPoint message defined in `goby/middleware/protobuf/geographic.proto`. You should then see it in the `goby zeromq subscribe` terminal:

```
> goby zeromq subscribe
...
1 | nav_group | goby.middleware.protobuf.LatLonPoint | 2025-Mar-11 23:32:18.002199 | lat: 42.5 lon: -70.3
```


## goby protobuf

Functions for dealing with Protobuf messages

### goby protobuf show

Displays the contents of a Protobuf message. 

For example:

```
goby protobuf show goby.acomms.protobuf.ModemTransmission
```

gives the full .proto definition:
```
message ModemTransmission {
  option (.dccl.msg) = {
    unit_system: "si"
  };
  enum TimeSource {
    MODEM_TIME = 1;
    GOBY_TIME = 2;
  }
  enum TransmissionType {
    DATA = 1;
    ACK = 2;
    DRIVER_SPECIFIC = 10;
  }
  optional int32 src = 1 [default = -1, (.goby.field) = {
    description: "modem ID of message source. 0 indicates BROADCAST."
  }, (.dccl.field) = {
    min: -1
    max: 30
  }];
  optional int32 dest = 2 [default = -1, (.goby.field) = {
    description: "modem ID of message destination. 0 indicates BROADCAST, -1 indicates QUERY_DESTINATION_ID (i.e., destination is set to the destination of the next available packet"
  }, (.dccl.field) = {
    min: -1
    max: 30
  }];

// ...
```

You can use `-l` to load shared libraries containing your custom Protobuf messages, or set the environmental variable GOBY_TOOL_LOAD_SHARED_LIBRARY.