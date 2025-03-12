# goby-zeromq: goby_logger and goby_playback


Logging is a critical part of robotic operations for many purposes, such as data collection for research, performance analysis, debugging, etc.

The `goby_logger` application allows you to log all or part of the messages being sent on the `interprocess` layer between various Goby ZeroMQ applications. These messages are written to a binary .goby file, which can be used to play back messages or for direct post mission analysis by converting to HDF5, JSON, text or other formats.

## goby_logger

At its simplest, `goby_logger` can be run with just a directory to log to, for example:

```
goby_logger --log_dir /var/log/goby
```

The directory must exist and must be writable prior to running goby_logger. The default settings would log all publications to `interprocess` regardless of type or group.

Log files are formatted `{platform-name}_{date-time}.goby` where `{platform-name}` is the value of `--interprocess 'platform: "{platform-name}"'` and `{date-time}` is the start time of `goby_logger` in UTC using the format YYYYMMDDTHHMMSS.

### Configuring goby_logger

This section covers some of the important `goby_logger` configuration settings:

- `log_dir`: Path to the directory (must exist) to write logs to 
- `type_regex`: C++ regex for which `types` to include in the log (e.g., for PROTOBUF this would be the message name). Defaults to `".*"` (all types).
- `group_regex`: C++ regex for which Goby `groups` to include in the log. Defaults to `".*"` (all groups).
- `load_shared_library`: Path to shared library that contains Protobuf messages. If all Protobuf messages are known to `goby_logger` at runtime then they are embedded in the log. This is highly recommended to do as this means that tools like `goby log convert` can run correctly even without the original .proto messages used at the time of logging, avoiding the situation where older logs cannot easily be converted if the original messages are not available.

### goby_logger subscriptions

The key subscription for `goby_logger` (besides all the messages to be logged) is:

```
- group: goby::logger::request
  scheme: PROTOBUF
  type: goby::middleware::protobuf::LoggerRequest
```

If you publish this message you can start/stop or rotate the log file, based on the contents of the message:

```
> goby protobuf show goby.middleware.protobuf.LoggerRequest
message LoggerRequest {
  enum State {
    START_LOGGING = 1;
    STOP_LOGGING = 2;
    ROTATE_LOG = 3;
  }
  required .goby.middleware.protobuf.LoggerRequest.State requested_state = 1;
  optional bool close_log = 2 [default = false];
}
```

For example I can stop logging using:

```
goby zeromq publish goby::logger::request goby.middleware.protobuf.LoggerRequest 'requested_state: STOP_LOGGING'
```

For use in C++ applications, the groups are defined in:

```
#include <goby/middleware/log/groups.h>
```

and the Protobuf messages are in [`goby/src/middleware/protobuf/logger.proto`](https://github.com/GobySoft/goby3/blob/3.0/src/middleware/protobuf/logger.proto) and can be included using:

```
#include <goby/middleware/protobuf/logger.pb.h>
```

## Publish/Subscribe API Diagram

![goby_clang_tool generated API figure](images/goby_logger_stub_deployment.png)

## goby log convert

The `goby log convert` function of the `goby` tool can be used to convert .goby files written by `goby_logger` into more usable formats.

For help see `goby log help convert`.

### Text

This is the default output. To convert all logs within a directory (same as log_dir given to `goby_logger`):
```
goby log convert /var/log/goby
```

The output is the same as the input but with the ".txt" extension instead of the ".goby".

If you have a lot of logs and want to make use of multiple CPU cores:

```
goby log convert -j16 /var/log/goby
```

### JSON

To use a non-default output, simply use the `--format` parameter:

```
goby log convert /var/log/goby/myfile.goby --format JSON
```

JSON output uses the same name as the input but with the ".json" extension taking the place of the ".goby", so "/var/log/goby/myfile.json" in the example above.

You can also explicitly change the output. For example you can write to stdout to pipe to another tool:

```
goby log convert /var/log/goby/myfile.goby -o - --format JSON | jq
```

which will give you a pretty-print output using the jq tool.

### HDF5

[HDF5](https://www.hdfgroup.org/solutions/hdf5/) is a widely used scientific data format, and is supported by common tools like Python, MATLAB, Octave, etc.

```
goby log convert /var/log/goby --format HDF5
```

Here the default extension for the output is `.h5`. 

To inspect a single file using `h5dump` you could run something like:

```
goby log convert /var/log/goby/myfile.goby --format HDF5 && h5dump /var/log/goby/myfile.h5
```

## goby_playback

`goby_playback` simply writes some subset of the messages to the current `gobyd` `interprocess` layer from an existing log.

At its simplest, start a new default gobyd:
```
gobyd
```

And run playback
```
goby_playback /var/log/goby/myfile.goby
```

You can view the new publications from the log using:

```
goby zeromq subscribe
```

You can adjust the playback rate to be faster than real time by setting `--rate=N` where N is the multiplier of the real speed to use.

You can filter the variables to publish to choose only a few groups and or types by using the `--group_regex` and/or `--type_filter` parameters.

Finally, if you didn't load the Protobuf messages when you ran `goby_logger` then you need to provide them to `goby_playback` using `--load_shared_library`.
