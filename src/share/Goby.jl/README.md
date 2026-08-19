# Goby.jl – Julia Support for Goby

This package provides the ability to run Goby applications from the Julia language. The Julia CxxWrap module is used to wrap the Goby C++ implementation (except for the `interthread` layer which is directly implemented in Julia) in order to keep the Julia implementation lightweight and feature-compliant with the C++ implementation.

---

## File Overview

| File | Purpose |
|------|---------|
| `src/Goby.jl` | **Main Julia module.** Wraps the CxxWrap'd C++ Goby application class and exposes `publish`, `subscribe`, `run`, `cfg`, and `read_cli_cfg` to Julia user code. |
| `src/gen_goby.jl` | **Code generator.** Reads an `interface.yml` file and emits two files: a C++ source file containing the `publish`/`subscribe` glue code for all declared transport layers, and a Julia module holding the declared groups and one accessor per portal. Invoked at build time by CMake. |
| `src/GobyMultiThread.jl` | `MultiThread` sub-module. Not directly included from user code. Implements interthread communication using Julia `Channel`s and `ThreadPools`, enabling multi-threaded Julia Goby apps where each task runs on a dedicated Julia thread. |
| `src/pkg.jl` | Minimal helper script used by CMake to install/instantiate the Julia package dependencies (`Pkg.instantiate()`). |
| `Project.toml` | Julia package manifest; declares dependencies (`CxxWrap`, `ProtoBuf`, `YAML`, `ThreadPools`) and package metadata. |

---

## `interface.yml` Reference

Julia is not a static language, but C++ is, and Goby3 relies heavily on the benefits of static analysis (e.g., through the use of constexpr Groups). 

To reconcile this for Julia, we require that the application author write a definition of the interfaces that the Julia application will use to publish and subscribe on. This is the `interface.yml` file and `gen_goby.jl` reads this file and generates the C++ glue code. 

Thus, the Julia application author does not need to directly write any C++ code, just this .yml file and the Julia code.

### Top-level keys

| Key | Required | Description |
|-----|----------|-------------|
| `application` | **Yes** | Application metadata (see below). |
| `interthread` | No | Interthread-layer publish/subscribe declarations. |
| `interprocess` | No | Interprocess-layer publish/subscribe declarations. |
| `intermodule` | No | Intermodule-layer publish/subscribe declarations. |

### `application`

```yaml
application:
  name: <CamelCase application name>      # used as the C++ class name and Julia type name
  cpp_type: <C++ template base class>     # e.g. goby::middleware::SingleThreadApplication
  config:                                 # configuration at launch, same as Config template parameter to the cpp_type class
    scheme: PROTOBUF                      # only PROTOBUF is currently supported
    type: <protobuf message type>         # dot-separated (Julia/proto) or ::-separated (C++)
```

### Transport layers (`interthread`, `interprocess`, `intermodule`)

Each layer accepts either a **single mapping** (one portal) or an **array of mappings** (multiple portals, each with its own `alias` which is corresponds to the portal function name in the class given to `application: cpp_type`). For example, if your `interprocess` portal is accessed via `interblock()`, use `alias: interblock`). For the Goby3 reference implementation, these function names are identical to the layer names and so alias should be omitted.

#### Single portal (mapping form): typical usage

```yaml
interprocess:
  publishes:
    - group: <C++ group expression>
      scheme: PROTOBUF
      type: <protobuf message type>
  subscribes:
    - group: <C++ group expression>
      scheme: PROTOBUF
      type: <protobuf message type>
```

#### Multiple portals (array form): advanced usage

When running a custom Application class that supports multiple INTERPROCESS layer transporters, you will need to use this form to distinguish them.

```yaml
interprocess:
  - alias: interprocess_zeromq  # uses Application::interprocess_zeromq()
    publishes:
      - group: <C++ group expression>
        scheme: PROTOBUF
        type: <protobuf message type>
  - alias: interprocess_udpm    # uses Application::interprocess_udpm()
    subscribes:
      - group: <C++ group expression>
        scheme: PROTOBUF
        type: <protobuf message type>
```

Each entry in the array may independently declare `publishes` and/or `subscribes`, and uses its own `alias` as the C++ accessor function name (i.e., the `LAYER_FUNCTION` in the generated macros).

#### Per-publication / per-subscription required fields

| Field | Required | Description |
|-------|----------|-------------|
| `group` | **Yes** | C++ group expression (e.g. `project::groups::my_group`). |
| `scheme` | **Yes** | Marshalling scheme. Currently only `PROTOBUF` is supported. |
| `type` | **Yes** | Protobuf message type. May use either dot-separated (Julia/proto) notation (`project.protobuf.MyMsg`) or C++ double-colon notation (`project::protobuf::MyMsg`). Both are normalised to `::` in the generated C++ code. |

---

### Complete `interface.yml` example

```yaml
application:
  name: JuliaDemo
  cpp_type: goby::middleware::SingleThreadApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.JuliaDemoConfig

interprocess:
  publishes:
    - group: project::groups::modem_tx
      scheme: PROTOBUF
      type: project.protobuf.CommsTx
  subscribes:
    - group: project::groups::modem_rx
      scheme: PROTOBUF
      type: project.protobuf.CommsRx
```

### Generated C++ output (excerpt)

For the `interprocess` block above the generator emits:

```cpp
// publish() method body
GOBY_JULIA_IF_PUBLICATION(PROTOBUF, INTERPROCESS, interprocess, project::groups::modem_tx, "project::groups::modem_tx", project::protobuf::CommsTx)

// subscribe() method body
GOBY_JULIA_IF_SUBSCRIPTION(PROTOBUF, INTERPROCESS, interprocess, project::groups::modem_rx, "project::groups::modem_rx", project::protobuf::CommsRx)
```

The group appears twice: as the C++ expression for the Goby call, and as a string for matching
what Julia asked for. The two are not interchangeable — a group's runtime name is not necessarily
its C++ variable name, and Julia only knows the latter.

### Generated Julia output

Alongside it the generator writes `<target>_goby.jl`, defining a module named after the
application:

```julia
module JuliaDemoGoby

using Goby

const APPLICATION_NAME = "JuliaDemo"

module groups
const modem_rx = "project::groups::modem_rx"
const modem_tx = "project::groups::modem_tx"
end

interprocess() = Goby.INTERPROCESS

end # module JuliaDemoGoby
```

Groups are named by their last `::` component; a short name claimed by two different expressions
is left out rather than guessed at, and the expression still works as a string. The accessors are
functions because the layer constants arrive with the application library, which is loaded after
this file is included.

---

## Build Flow

```
interface.yml
      │
      │  gen_goby.jl (Julia, typically invoked by CMake add_custom_command)
      ▼
JuliaDemo.cpp          ← autogenerated C++ glue
      │
      │  C++ compiler  (links against goby, goby_zeromq, JlCxx::cxxwrap_julia)
      ▼
libJuliaDemo.so        ← shared library exposing CxxWrap module "JuliaDemo"
      │
      │  Julia `using JuliaDemo`  (at runtime)
      ▼
Julia user script (JuliaDemo.jl)
```

### Step-by-step

1. **Package installation** – CMake runs `pkg.jl` via Julia to call `Pkg.instantiate()`, downloading all dependencies declared in `Project.toml` into the build directory's `Manifest.toml`.

2. **Proto generation** – Use the Julia Protobuf.jl compiler to produce `*_pb.jl` files alongside the C++ `.pb.h`/`.pb.cc` files.

3. **Glue generation** – Use gen_goby.jl:
   ```
   julia --project=<Goby.jl> -L gen_goby.jl \
         -e 'goby_gen_cpp("interface.yml", "JuliaDemo.cpp", ["proto_header.pb.h"])' \
         -e 'goby_gen_julia("interface.yml", "julia_demo_goby.jl")'
   ```
   `goby_gen_julia` produces the module shown above. `goby_gen_cpp` produces `JuliaDemo.cpp`, which:
   - `#include`s `<goby/middleware/languages/julia/application.h>` and any additional headers passed as the third argument.
   - Defines `CONFIG_TYPE`, `APPLICATION_TYPE`, and `APPLICATION_NAME` macros.
   - Declares a class `JuliaDemo` extending `goby::middleware::julia::Application<APPLICATION_TYPE>` with `publish()` and `subscribe()` bodies built from `GOBY_JULIA_IF_PUBLICATION` / `GOBY_JULIA_IF_SUBSCRIPTION` macros.
   - Registers the CxxWrap module entry point via `GOBY_JULIA_DEFINE_MODULE(JuliaDemo)`.

4. **Shared library** – Compile `JuliaDemo.cpp` into `libJuliaDemo.so`, link against Goby, ZeroMQ transport, and JlCxx.

5. **Julia runtime** – The user Julia script loads `libJuliaDemo.so` through `CxxWrap` (typically via a thin wrapper module), then uses the `Goby` module (`Goby.jl`) to publish and subscribe.

---

## Single-Threaded Julia Usage Example

This example corresponds to the `interface.yml` above.

```julia
using Goby
# Load the generated CxxWrap module and application-specific protobuf types
# (exact using/import statements depend on your project layout)

# the module gen_goby.jl wrote from interface.yml, beside the application library
include(joinpath(@__DIR__, "julia_demo_goby.jl"))

# ---------- Callbacks ----------

function publish_outgoing_msg()
    msg = project.protobuf.CommsTx(request_id = 42)
    Goby.publish(goby_app, JuliaDemoGoby.interprocess(), JuliaDemoGoby.groups.modem_tx, msg)
    println("PUBLISHED: $msg")
end

function receive_incoming_msg(msg::project.protobuf.CommsRx)
    println("RECEIVED: $msg")
end

# Called periodically at `loop_frequency` Hz
function loop()
    t = time()
    println("$t: Julia loop")
    publish_outgoing_msg()
end

# ---------- Initialisation ----------

# Read the Protobuf TextFormat config file whose path is given as the first CLI argument
goby_app = Goby.JuliaDemo(Goby.read_cli_cfg())

# Pull protobuf config out of the app
pb_cfg = Goby.cfg(goby_app, project.config.protobuf.JuliaDemoConfig)
println("Configuration: my_value_a=$(pb_cfg.my_value_a); my_value_b=$(pb_cfg.my_value_b)")

goby_cfg = Dict(
    :loop_function  => loop,   # called at loop_frequency Hz
    :loop_frequency => 0.1,    # Hz
    :pb             => pb_cfg,
)

# ---------- Subscriptions ----------

function start()
    Goby.subscribe(goby_app, JuliaDemoGoby.interprocess(),
                   JuliaDemoGoby.groups.modem_rx,
                   receive_incoming_msg)
end

# ---------- Run ----------

# Blocks until the application is terminated (goby_terminate, SIGINT, or SIGTERM)
Goby.run(goby_app)
```

### Key `Goby` Julia API

| Function | Description |
|----------|-------------|
| `Goby.read_cli_cfg()` | Reads the TextFormat config file given as the first CLI positional argument and returns it as a `String`. |
| `Goby.JuliaDemo(cfg_str)` | Constructs the CxxWrap'd application object, parsing `cfg_str` as a Protobuf TextFormat message. Returns `app` used by the remaining functions. |
| `Goby.cfg(app, PbType)` | Deserialises and returns the application's configuration as a `PbType` protobuf message. |
| `Goby.publish(app, layer, group, msg)` | Publishes protobuf `msg` on `layer` (e.g. `Goby.INTERPROCESS`) to the given `group` string. |
| `Goby.subscribe(app, layer, group, callback)` | Subscribes to messages on `layer`/`group`; `callback` must accept a single argument of the expected protobuf type. |
| `Goby.run(app)` | Starts the Goby event loop (blocking). Calls `start()` and then enters the C++ run loop, invoking `loop()` at `goby_cfg[:loop_frequency]` Hz. |
| `Goby.run(app, Main, [TaskModuleA, …])` | Multi-threaded variant; each `TaskModule` is spawned on its own Julia thread. Requires `julia -t <N>` where N is at least the number of TaskModules plus 3 (e.g., -t 5 for an application with two TaskModules). |

---

## Multi-Threaded Julia Usage

`Goby.run(app, Main, [TaskModuleA, …])` spawns each task module on its own Julia thread. A task
module may define `start()`, called on its own thread before the event loop, and a `goby_cfg`
with `:loop_function` and `:loop_frequency` to have a function called periodically.

`Goby.publish` and `Goby.subscribe` work from any task, on any layer:

* `INTERTHREAD` is implemented in Julia, carrying messages between tasks over `Channel`s. It
  never reaches the C++ side, so an interthread group is a plain string chosen by the
  application rather than a group declared in `interface.yml`, and an interthread message can be
  any Julia value rather than only a protobuf message.
* `INTERPROCESS` and `INTERMODULE` belong to the C++ application, which lives on one task. A
  publication from any other task is handed to that task over an interthread channel, and a
  subscription is registered there and its messages delivered back to the task that asked for
  it. This is what a C++ thread's `InterProcessForwarder` does, so no task needs a portal of its
  own, and none has to route through `Main`.

The C++ task drains those channels from its `loop()`, whose rate `Main`'s `goby_cfg` sets:

```julia
goby_cfg = Dict(:cxx_channel_check_frequency => 10)   # Hz, the default
```

That rate bounds how long an interprocess publication from another task waits before it goes
out, so raise it for latency-sensitive traffic.
