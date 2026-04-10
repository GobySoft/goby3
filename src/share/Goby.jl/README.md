# Goby.jl – Julia Support for Goby

This package provides the Julia-side runtime and code-generation tooling that allows a Goby application to be driven from Julia code.

---

## File Overview

| File | Purpose |
|------|---------|
| `Project.toml` | Julia package manifest; declares dependencies (`CxxWrap`, `ProtoBuf`, `YAML`, `ThreadPools`) and package metadata. |
| `src/Goby.jl` | Main Julia module. Wraps the CxxWrap'd C++ Goby application class and exposes `publish`, `subscribe`, `run`, `cfg`, and `read_cli_cfg` to Julia user code. |
| `src/GobyMultiThread.jl` | `MultiThread` sub-module. Implements inter-task (interthread) communication using Julia `Channel`s and `ThreadPools`, enabling multi-threaded Julia Goby apps where each task runs on a dedicated Julia thread. |
| `src/gen_goby.jl` | **Code generator.** Reads an `interface.yml` file and emits a C++ source file containing the `publish`/`subscribe` glue code for all declared transport layers. Invoked at build time by CMake. |
| `src/pkg.jl` | Minimal helper script used by CMake to install/instantiate the Julia package dependencies (`Pkg.instantiate()`). |

---

## `interface.yml` Reference

The interface file describes the Goby application and every message the Julia code is allowed to publish or subscribe to.  `gen_goby.jl` reads this file and generates the C++ glue code.

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
  config:
    scheme: PROTOBUF                      # only PROTOBUF is currently supported
    type: <protobuf message type>         # dot-separated (Julia/proto) or ::-separated (C++)
```

### Transport layers (`interthread`, `interprocess`, `intermodule`)

Each layer accepts either a **single mapping** (one portal) or an **array of mappings** (multiple portals, each with its own `alias`).

#### Single portal (mapping form)

```yaml
interprocess:
  alias: my_portal            # optional; defaults to the layer name if omitted
  publishes:
    - group: <C++ group expression>
      scheme: PROTOBUF
      type: <protobuf message type>
  subscribes:
    - group: <C++ group expression>
      scheme: PROTOBUF
      type: <protobuf message type>
```

#### Multiple portals (array form)

```yaml
interprocess:
  - alias: portal_a
    publishes:
      - group: <C++ group expression>
        scheme: PROTOBUF
        type: <protobuf message type>
  - alias: portal_b
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
  - alias: interblock
    publishes:
      - group: project::host_interface::groups::outgoing_modem_transmission
        scheme: PROTOBUF
        type: project.protobuf.CommunicationsRequestOrReport
  - alias: interblock_udpm
    publishes:
      - group: project::host_interface::groups::udpm_test
        scheme: PROTOBUF
        type: project.protobuf.Example

intermodule:
  subscribes:
    - group: project::intermodule::groups::incoming_modem_message
      scheme: PROTOBUF
      type: project.protobuf.CommunicationsRequestOrReport
```

### Generated C++ output (excerpt)

For the `interprocess` block above the generator emits:

```cpp
// publish() method body
GOBY_JULIA_IF_PUBLICATION(PROTOBUF, INTERPROCESS, interblock, project::host_interface::groups::outgoing_modem_transmission, project::protobuf::CommunicationsRequestOrReport)
GOBY_JULIA_IF_PUBLICATION(PROTOBUF, INTERPROCESS, interblock_udpm, project::host_interface::groups::udpm_test, project::protobuf::Example)

// subscribe() method body
GOBY_JULIA_IF_SUBSCRIPTION(PROTOBUF, INTERMODULE, intermodule, project::intermodule::groups::incoming_modem_message, project::protobuf::CommunicationsRequestOrReport)
```

---

## Build Flow

```
interface.yml
      │
      │  gen_goby.jl (Julia, invoked by CMake add_custom_command)
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

2. **Proto generation** – `PROTOBUF_GENERATE_JULIA` and `generate_julia_protos()` CMake helpers invoke the Julia protobuf compiler to produce `*_pb.jl` files alongside the C++ `.pb.h`/`.pb.cc` files.

3. **C++ glue generation** – `GOBY_GENERATE_JULIA(OUTPUT_TARGET INTERFACE_YML CONFIG_PROTO …)` invokes `gen_goby.jl`:
   ```
   julia --project=<Goby.jl> -L gen_goby.jl \
         -e 'goby_gen_cpp("interface.yml", "JuliaDemo.cpp", ["proto_header.pb.h"])'
   ```
   This produces `JuliaDemo.cpp`, which:
   - `#include`s `<goby/middleware/languages/julia/application.h>` and any additional headers.
   - Defines `CONFIG_TYPE`, `APPLICATION_TYPE`, and `APPLICATION_NAME` macros.
   - Declares a class `JuliaDemo` extending `goby::middleware::julia::Application<APPLICATION_TYPE>` with `publish()` and `subscribe()` bodies built from `GOBY_JULIA_IF_PUBLICATION` / `GOBY_JULIA_IF_SUBSCRIPTION` macros.
   - Registers the CxxWrap module entry point via `GOBY_JULIA_DEFINE_MODULE(JuliaDemo)`.

4. **Shared library** – CMake compiles `JuliaDemo.cpp` into `libJuliaDemo.so`, linked against Goby, ZeroMQ transport, and JlCxx.

5. **Julia runtime** – The user script loads `libJuliaDemo.so` through `CxxWrap` (typically via a thin wrapper module), then uses the `Goby` module (`Goby.jl`) to publish and subscribe.

---

## Single-Threaded Julia Usage Example

This example corresponds to the `interface.yml` above.

```julia
using Goby
# Load the generated CxxWrap module and application-specific protobuf types
# (exact using/import statements depend on your project layout)
# using JuliaDemo
# using project.protobuf

# ---------- Callbacks ----------

function publish_outgoing_msg()
    msg = project.protobuf.CommunicationsRequestOrReport(request_id = 42)
    Goby.publish(goby_app, Goby.INTERPROCESS, "project::host_interface::groups::outgoing_modem_transmission", msg)
    println("PUBLISHED: $msg")
end

function receive_incoming_msg(msg::project.protobuf.CommunicationsRequestOrReport)
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

# Pull typed config out of the app
pb_cfg = Goby.cfg(goby_app, project.config.protobuf.JuliaDemoConfig)
println("Configuration: my_value_a=$(pb_cfg.my_value_a); my_value_b=$(pb_cfg.my_value_b)")

goby_cfg = Dict(
    :loop_function  => loop,   # called at loop_frequency Hz
    :loop_frequency => 0.1,    # Hz
    :pb             => pb_cfg,
)

# ---------- Subscriptions ----------

function start()
    Goby.subscribe(goby_app, Goby.INTERMODULE,
                   "project::intermodule::groups::incoming_modem_message",
                   receive_incoming_msg)
end

# ---------- Run ----------

# Blocks until the application is terminated (goby_terminate, SIGINT, or SIGTERM)
Goby.run(goby_app)
```

### Key `Goby` API

| Function | Description |
|----------|-------------|
| `Goby.read_cli_cfg()` | Reads the TextFormat config file given as the first CLI positional argument and returns it as a `String`. |
| `Goby.JuliaDemo(cfg_str)` | Constructs the CxxWrap'd application object, parsing `cfg_str` as a Protobuf TextFormat message. |
| `Goby.cfg(app, PbType)` | Deserialises and returns the application's configuration as a `PbType` protobuf message. |
| `Goby.publish(app, layer, group, msg)` | Publishes protobuf `msg` on `layer` (e.g. `Goby.INTERPROCESS`) to the given `group` string. |
| `Goby.subscribe(app, layer, group, callback)` | Subscribes to messages on `layer`/`group`; `callback` must accept a single argument of the expected protobuf type. |
| `Goby.run(app)` | Starts the Goby event loop (blocking). Calls `start()` and then enters the C++ run loop, invoking `loop()` at `goby_cfg[:loop_frequency]` Hz. |
| `Goby.run(app, Main, [TaskModuleA, …])` | Multi-threaded variant; each `TaskModule` is spawned on its own Julia thread. Requires `julia -t <N>`. |
