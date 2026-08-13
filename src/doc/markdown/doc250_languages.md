# Other languages (Julia, Python) {#doc250_languages}

Goby applications can be written in Julia or Python as well as C++. Both bindings work the same
way, and both are driven by the same interface definition file.

## Why an interface file

Goby3 leans on static analysis: groups are `constexpr`, message types and marshalling schemes are
template parameters, and the transporter to use is chosen by which accessor you call
(`interprocess()`, `intermodule()`, ...). Dynamic languages have none of that at compile time.

Rather than give up those guarantees, the bindings ask the application author to declare the
application's publish/subscribe interface up front, in an `interface.yml` file. A generator turns
that declaration into C++ glue code, which makes the ordinary, statically typed Goby calls. The
application author writes the `interface.yml` and their Julia or Python code, and never any C++.

The format is shared exactly between the two languages and is documented in
`share/goby/interface/README.md`, with a machine-readable schema alongside it. A publication or
subscription that was not declared fails at runtime with a message naming the layer, group, type
and scheme it could not find.

## Build flow

```
interface.yml
      |
      |  gen_goby.jl (Julia) or goby_gen_cpp (Python)
      |
      +--> <App>.cpp ----- C++ compiler --> lib<App>.so
      |                    (links goby, goby_zeromq, and the binding library)
      |
      +--> <app>_goby.py -- imported by ---^
           (Python only)     application code
```

\image html julia-sequence.png "Sequence of calls between the Julia code, the generated C++ glue and Goby"

## Python

The Python API is class-based, mirroring the C++ application model: the application is a class,
subscriptions happen in its constructor, `loop()` is an override, and `goby.run()` stands in for
`goby::run<App>(argc, argv)`.

```python
import sys

import goby
from python_demo_goby import SingleThreadApplication, groups
from project.protobuf import comms_pb2


class PythonDemo(SingleThreadApplication):
    def __init__(self):
        super().__init__(loop_frequency_hertz=10)
        # self.cfg is the application configuration, already decoded
        self.interprocess().subscribe(groups.modem_rx, comms_pb2.CommsRx, self.on_rx)

    def on_rx(self, msg: comms_pb2.CommsRx) -> None:
        print(f"RECEIVED: {msg}")

    def loop(self) -> None:
        self.interprocess().publish(groups.modem_tx, comms_pb2.CommsTx(request_id=42))


if __name__ == "__main__":
    sys.exit(goby.run(PythonDemo))
```

The base class (`SingleThreadApplication` above) and the `groups` constants come from the module
generated from `interface.yml`, which is named after the application: an application called
`PythonDemo` produces `python_demo_goby`.

### How C++ concepts map

| C++ | Python |
|-----|--------|
| `class App : public SingleThreadApplication<Cfg>` | `class App(SingleThreadApplication)` |
| `void loop() override` | `def loop(self)` |
| `void initialize() override`, `void finalize() override` | `def initialize(self)`, `def finalize(self)` |
| `interprocess().publish<group>(msg)` | `self.interprocess().publish(group, msg)` |
| `subscribe<group, Type>(callback)` | `subscribe(group, Type, callback)`, or `subscribe(group, callback)` to infer `Type` from the callback's annotation |
| `app_cfg()` | `self.cfg` |
| `quit()` | `self.quit()` |
| `app_name()` | `self.app_name()` |
| `goby::run<App>(argc, argv)` | `goby.run(App)` |

A layer declared with an `alias` in `interface.yml` becomes an accessor of that name, exactly as
it is an accessor of that name in C++.

### Configuration and the command line

`goby.run()` reads the command line with `goby::middleware::ProtobufConfigurator`, the same class
C++ applications use, so a Python application accepts the same command line: `--help`,
`--example_config`, `-v`, `--app_name`, per-field overrides, and a configuration file. As in C++,
`--help` and `--example_config` print and exit.

For embedding an application in a larger program, or for tests that should not need a command
line, the configuration can be passed directly:

```python
goby.run(PythonDemo, config=PythonDemoConfig(...))     # a protobuf message
goby.run(PythonDemo, config_text="my_value_a: 42")     # Protobuf TextFormat
goby.run(PythonDemo, argv=["python_demo", "-v"])       # an explicit command line
```

An invalid configuration is reported the way a C++ application reports it, and `goby.run()`
returns a non-zero value rather than raising or exiting.

### Threads, signals and the GIL

The Goby event loop runs in C++ with the GIL released, and each callback into Python reacquires
it. While the loop is running, Goby installs its own `SIGINT`/`SIGTERM` handlers in place of the
interpreter's, so that Ctrl-C asks the application to quit cleanly rather than raising a
`KeyboardInterrupt` from inside a C++ call stack. An application that is blocked waiting for data
(no loop frequency, nothing arriving) will not notice the request until something wakes it; a
second Ctrl-C restores the default handler and terminates the process.

Multi-threaded Python applications are not yet supported; use `SingleThreadApplication`.

### Building

`find_package(goby)` provides `goby_add_python_app()`:

```cmake
goby_add_python_app(
  TARGET my_app
  INTERFACE_YML ${CMAKE_CURRENT_SOURCE_DIR}/interface.yml
  SOURCES ${PROTO_SRCS} ${PROTO_HDRS}
  INCLUDES goby/zeromq/application/single_thread.h project/comms.pb.h project/groups.h
  PROTO_MODULES project.comms_pb2
  LINK_LIBRARIES goby goby_zeromq)
```

`INCLUDES` are the C++ headers the generated glue needs: the application class named by
`application: cpp_type`, the protobuf headers for the declared message types, and the header
declaring the groups. `PROTO_MODULES` are the corresponding generated Python protobuf modules,
which the generated module imports so that message types can be resolved by name.

Goby must be built with `-Dbuild_python=ON`, which requires `pybind11-dev`.

### Installation notes

The `goby` Python package is pure Python; all the compiled code lives in the extension module
generated for each application. Goby's own `.proto` files are compiled to Python and installed
alongside the package, because every application configuration embeds
`goby.middleware.protobuf.AppConfig` and protobuf will not register the same descriptor twice in
one process.

Because those generated modules land in a `goby` package of their own, `goby/__init__.py` extends
`__path__` the way the `google` namespace does, so the runtime package and the generated protobuf
modules can live in different directories without shadowing each other.

## Julia

The Julia bindings predate the Python ones and use CxxWrap.jl. Julia has no classes, so its API is
a set of functions taking the application object. See `share/goby/Goby.jl/README.md` for the
Julia API and usage examples.
