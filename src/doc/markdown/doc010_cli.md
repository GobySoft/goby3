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

## goby test

Runs declarative publish/subscribe tests against a Goby system, for example in CI or after a deployment, without writing a Goby application. It has three actions:

```
goby test run <test file> [-D NAME=value] [--repeat N] [-o HUMAN|JSON] [--output_file <path>] [--log_dir <dir>] [-l <shared library>]
goby test validate <test file> [-D NAME=value] [-o HUMAN|JSON] [-l <shared library>]
goby test visualize <test file> [-D NAME=value] [--output_file <file.dot>]
```

- `run` starts the test's fixtures, runs the test and stops the fixtures. It exits 0 when every expectation passed, 1 when one failed, and 2 when the test could not run (an invalid file, a fixture that never became ready, an endpoint that could not connect, a required application that never responded, or an interruption).
- `validate` checks the file without connecting to anything and lists every problem with its location (such as `test.run[2].expect[0].match`). It exits 0 when the file is valid and 1 otherwise. `run` performs the same checks first.
- `visualize` writes the test plan as a Graphviz graph, e.g. `goby test visualize nav.yml | dot -Tpng -o nav.png`.

The test file is YAML (`.yml`/`.yaml`), JSON (`.json`) or Protobuf TextFormat (any other extension). Its fields are the `TestFile` message in `goby/apps/middleware/test_tool/test_tool_config.proto`, with the test itself in `goby/middleware/protobuf/pubsub_test.proto`. YAML requires Goby built with yaml-cpp (`libyaml-cpp-dev`; `-Denable_yaml=ON`, the default when found).

### Test file

```yaml
endpoint:
  - { name: auv, zeromq: { platform: auv0 } }
  - { name: topside, zeromq: { platform: "${TOPSIDE:-topside}", transport: TCP, ipv4_address: 10.0.0.5 } }
  - { name: payload, udpm: { platform: auv0, multicast_port: 11150 } }

fixture:
  - name: auv0
    command: goby_launch -r auv0.launch
    cwd: ../launch
    ready: { output_regex: "All processes running", timeout: 30 }

test:
  name: nav forwarding
  default_timeout: 5
  require_app:
    - { endpoint: auv, name: [gobyd, goby_frontseat_interface], timeout: 30 }
  run:
    - { name: settle, wait: 10 }
    - name: nav reaches topside
      publish:
        - { endpoint: auv, group: nav, type: goby.middleware.protobuf.LatLonPoint, value: "lat: 42.5 lon: -70.3" }
      expect:
        - endpoint: topside
          group: nav
          type: goby.middleware.protobuf.LatLonPoint
          match: "lat: 42.5"
          condition: [{ field: lon, op: LT, value: 0 }]
    - name: status, no raw passthrough, payload heartbeat
      mode: PARALLEL
      step:
        - name: status
          expect: [{ endpoint: auv, group: status, type: JSON/Status, min_count: 3, timeout: 10 }]
        - name: no raw
          expect: [{ endpoint: topside, group: raw, count: 0, timeout: 3 }]
        - name: payload heartbeat
          expect: [{ endpoint: payload, group: heartbeat, min_count: 2, timeout: 10 }]
```

![](images/goby-test-plan.png)

- `endpoint`: named connections, each with `zeromq` settings (one `gobyd`) or `udpm` settings (one multicast group), on the interprocess (`LAYER_INTERPROCESS`, the default), intermodule (`LAYER_INTERMODULE`) or intervehicle (`LAYER_INTERVEHICLE`, see below) layer. A test may mix transports. An endpoint with neither uses zeromq when Goby was built with it, otherwise udpm; with no endpoints, one named `default` is created. Endpoint settings are layered over `run`'s `--zeromq` (also `GOBY_INTERPROCESS`) and `--udpm` settings.
- `fixture`: commands (run with `/bin/sh -c`, in `cwd`, each in its own process group) started in order before the test, each waiting until it is `ready`: every `output_regex` (one or more) has matched a line of output within `timeout`, in any order, then an optional `delay`. After the test they are stopped in reverse order with `stop.signal` (default `SIGINT`), escalating to `SIGTERM` and `SIGKILL` after `stop.grace` seconds. On Linux, `goby test` also stops descendants that left the process group (such as detached `screen` sessions). Each fixture's output is written to a log in `--log_dir` (a new temporary directory by default); a failed test's report includes the log path and its last lines.
- `test.require_app`: application names that must answer the coroner health request before the runs start; a running `goby_coroner`'s report is used instead once seen.
- `test.run`: runs execute `SEQUENTIAL` (default) or `PARALLEL` as given by `test.mode`. A run holds `publish`, `expect` and `wait` entries directly, or a list of `step` entries executed by its own `mode`. A `wait` keeps a run or step open for at least that long; on its own it is a pause.

Within a run or step the expectations are subscribed first, then the publications are sent (after `delay`), then the tool waits until every expectation is satisfied or times out. An expectation names an endpoint, a `group` regex and a `type` regex (optionally prefixed by a scheme, such as `DCCL/`), and passes when the number of matching messages satisfies `min_count` (default 1, done as soon as it is reached) or `count` (exact; the step lasts its whole timeout so that extra messages are seen, and `count: 0` asserts nothing arrives). A message matches when it satisfies `match`, a partial message in the same syntax as `publish` (TextFormat for Protobuf and DCCL, JSON for JSON), and every `condition` (a `field` path such as `pos.lat` or `waypoint[0].id`, an `op` of `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `REGEX`, `EXISTS` or `ABSENT`, and a `value`). Messages that fail to match are counted as rejected and the reason for the last rejection is reported. Durations are in seconds.

### Intervehicle endpoints

A `LAYER_INTERVEHICLE` endpoint publishes and subscribes through its `gobyd`'s intervehicle links, as an application does, so a test with one endpoint per vehicle (each with its own `gobyd` fixture) checks traffic across the link.

- Types are DCCL only, and groups are numeric (`group: "7"`; `"0"` is broadcast). Any other group is carried in the message: `group_field` names the top-level integer field that holds it.
- An expectation is also a subscription, made once `require_app` has passed and before the first run. `intervehicle { publisher_id: 1 }` lists the modem ids it is forwarded to, and a publish needs a `delay` that lets it cross the link.
- A publish's `intervehicle` sets its buffer (`ttl`, `max_queue`, `ack_required`, ...), `broadcast` and `clear_pending`. With `ack_required: true`, its acks and expiries are reported on the publishing endpoint, in its group, as `PROTOBUF/goby.middleware.intervehicle.protobuf.AckData` and `ExpireData`.

```yaml
endpoint:
  - { name: auv, layer: LAYER_INTERVEHICLE, zeromq: { platform: auv } }
  - { name: topside, layer: LAYER_INTERVEHICLE, zeromq: { platform: topside } }
test:
  run:
    - publish:
        - endpoint: auv
          group: "7"
          group_field: group
          type: DCCL/NavigationReport
          value: "x: 10 y: 20"
          intervehicle: { buffer: { ack_required: true } }
          delay: 3
      expect:
        - { endpoint: topside, group: "7", group_field: group, type: NavigationReport, intervehicle: { publisher_id: [1] } }
        - { endpoint: auv, group: "7", type: PROTOBUF/goby.middleware.intervehicle.protobuf.AckData }
```

### Repeating runs

A run with `repeat: N` runs N times in sequence. A run with `for_each` runs once per entry, with `${NAME}` anywhere in the run (its name, groups, types, publish values, `match` and conditions) replaced by that entry's value; with both, each entry runs `repeat` times. `${ITERATION}` is the pass number, from 1. Each pass is reported as its own run, named after the run (or `<name> [k/N]` when the name doesn't use a variable), and `validate` and `visualize` show every pass.

```yaml
run:
  - { name: nav round trip, repeat: 5, publish: [...], expect: [...] }
  - name: heading ${HEADING} is ${DIR}
    for_each:
      - { HEADING: 0, DIR: N }
      - { HEADING: 90, DIR: E }
    publish: [{ group: cmd, type: JSON/Heading, value: '{"heading": ${HEADING}}' }]
    expect: [{ group: status, type: JSON/Status, match: '{"dir": "${DIR}"}' }]
```

`for_each` names must not also be set by `-D` or the environment, which replace `${NAME}` when the file is read (`validate` reports this). In TextFormat an entry is written `for_each { var { key: "HEADING" value: "0" } ... }`.

`goby test run --repeat N` runs the whole test N times with the fixtures left running, and fails if any pass fails; the report lists each pass, with details for those that didn't pass.

### Reusing test files

- `${NAME}` and `${NAME:-default}` are replaced by `-D NAME=value`, then the environment, then the default (or a run's `for_each`); `${TEST_DIR}` is the directory of the file being read, and `$${` is a literal `${`. A name that is never set is reported by `validate`.
- `include: [endpoints.yml]` merges other files (of any format) before this one, so endpoints or fixtures can be shared.
- `load_shared_library` entries, `include` paths and fixture `cwd` resolve relative to the file that declares them. Libraries can also be given with `-l` or `GOBY_TOOL_LOAD_SHARED_LIBRARY` as for `publish` and `subscribe`.
- In CMake, `find_package(GOBY)` sets `GOBY_EXECUTABLE` to the `goby` tool from the same build or installation as the libraries, e.g. `add_test(NAME nav COMMAND ${GOBY_EXECUTABLE} test run nav.yml)`.

## goby interface

Works with the publish/subscribe interface files that `goby_clang_tool -gen` writes for each application (`<target>_interface.yml`, see `cmake_modules/GobyClangTool.cmake`), and the deployment files that place those applications on platforms and modules (see [Interfaces and deployments](doc260_interfaces.md)):

```
goby interface validate <deployment.yml> [-D NAME=value] [-I <dir>] [-o HUMAN|JSON] [--output_file <path>] [--strict] [--report_unused] [--assume_dynamic] [--omit_group_regex <regex>] [--omit_node_regex <regex>]
goby interface visualize <deployment.yml> [-D NAME=value] [-I <dir>] [--format DOT|D2] [--output_file <file>] [--expand_instances] [--no_disconnected] [--omit_interthread] [--omit_group_regex <regex>] [--omit_node_regex <regex>] [--include_terminate] [--include_coroner] [--include_all] [--splines <setting>]
```

- `validate` checks that every `required` subscription has a publisher in its scope (`required-unmet`, an error) and every `recommended` one (`recommended-unmet`, a warning), along with problems reading the files. It exits 0 when the deployment is valid, 1 on an error (or a warning, with `--strict`) and 2 when the deployment file cannot be read. `-o JSON` writes the `InterfaceValidation` message in `goby/apps/middleware/interface_tool/interface_tool_config.proto`.
- `visualize` writes the deployment as a Graphviz graph, e.g. `goby interface visualize fleet.yml | dot -Tsvg -o fleet.svg`, or with `--format D2` as a [D2](https://d2lang.com) diagram, e.g. `goby interface visualize fleet.yml --format D2 > fleet.d2 && d2 fleet.d2 fleet.svg`. Both use the same colors and edge styles: bold for required, tapered (dashed in D2) for recommended, dotted for regex and dashed for dynamic groups, with full group, scheme and type in each tooltip. The D2 output sets the ELK layout, which handles connections between nested containers better than D2's default. `goby_clang_tool -viz` (`-format=dot|d2`) draws the same graph, and `goby_visualize_interfaces(... FORMAT D2 ...)` renders it in CMake.
- Either takes a single interface file as a one-platform deployment.

```yaml
deployment: fleet
platforms:
  - name: auv
    run:
      - goby_gps
      - nav_manager
      - { application: goby_moos_gateway, plugins: [project_moos_gateway_plugin] }
      - { application: goby_modemdriver, name: modem_acoustic }
  - name: topside
    modules:
      - { name: shore, run: [goby_liaison] }
```

```
$ goby interface validate fleet.yml
fleet.yml: invalid, 1 error (2 platforms, 5 applications, 31 connections, 2/3 required met)
  error    required-unmet         auv/nav_manager [interprocess] no publisher for nav::depth (PROTOBUF nav::protobuf::Depth)
```

- A platform lists what it `run`s, or groups that into `modules`, each one `gobyd`. Connections are matched within each layer's scope: interthread within an application, interprocess within a module, intermodule across the modules of a platform, and intervehicle between platforms.
- A `run` entry names an application, and the interface file that declares it is found beside the deployment file, then in each `-I` directory and `GOBY_INTERFACE_PATH` (colon separated), then in Goby's own `share/goby/interfaces`. `plugins` merges plugin interfaces into their host application, `name` tells two instances of one application apart, and `file` gives an interface file by path. A list of files under `interfaces` (each optionally `{ file: ..., application: ... }`) is also accepted.
- `${NAME}` and `${NAME:-default}` are replaced by `-D NAME=value`, then the environment. `${GOBY_INTERFACES_DIR}` is Goby's own interface directory.
### Dynamic groups, instances and overrides

`goby_clang_tool -gen` records publications and subscriptions made with `publish_dynamic()` and `subscribe_dynamic()` on a `DynamicGroup` as `dynamic: true`. When the group's string is known but its numeric part is set at run time (`DynamicGroup(groups::rx, cfg().modem_id())`), it adds `numeric: runtime`; when the string itself is set at run time, the group is `"*"` and `expr` holds the C++ expression. The deployment file supplies what the code cannot:

```yaml
deployment: fleet
platforms:
  - name: hub
    run: [hub_manager]
    bind:
      - { group: fleet::hub_command, numeric: "151-153" }
      - dynamic: { layer: interprocess, type: google::protobuf::Message }
        publishes:
          - { group: fleet::engineering, type: fleet::protobuf::Engineering }
  - name: bot
    instances: [1, 2, 3]
    run: [mission_manager]
    bind:
      - { group: fleet::hub_command, numeric: "${instance} + 150" }
necessity:
  - { application: mission_manager, group: fleet::hub_command, set: required }
```

- `bind` (on a platform, or on one `run` entry, which takes precedence) gives the numeric part of matching groups (`*` is a wildcard), as a number, a range `a-b`, a list, or arithmetic on `${instance}`; or, with `dynamic:` selecting by layer, scheme and type, the groups and types a fully dynamic publication or subscription stands for.
- `instances` makes one platform per value, named `<name><instance>`. `validate` checks each; `visualize` draws the platform once (its binds taking every instance's numerics) unless `--expand_instances` is given.
- `necessity` sets the necessity of the subscriptions it selects (by `platform`, `module`, `application`, `thread`, `layer`, `group` and `type` regexes) in this deployment, whatever the code said.
- A dynamic group with its numeric still unknown reaches subscribers of the same group, drawn dashed, but does not satisfy a required subscription unless `--assume_dynamic` is given. A group whose string is unknown reaches nothing. `validate` reports these as `dynamic-unbound` (warning) and `dynamic-unresolved` (info), and binds or overrides that select nothing as `bind-unused` and `necessity-unused` (warnings).

- In CMake, `goby_validate_interfaces(DEPLOYMENT_YAML fleet.yml INTERFACE_DIRS ${YML_OUT_DIR})` (from `find_package(GOBY)`) adds a test that runs `goby interface validate --strict`.

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