# Other languages (Julia, Python) {#doc250_languages}

Goby applications can be written in Julia or Python as well as C++. Both bindings work the same
way, and both are driven by the same interface definition file.

## Why an interface file

Goby3 emphasizesstatic analysis: groups are `constexpr`, message types and marshalling schemes are
template parameters, and the transporter to use is chosen by which accessor you call
(`interprocess()`, `intermodule()`, ...). Dynamic languages like Python and Julia have none of that at compile time.

Rather than give up those guarantees, the bindings ask the application author to still declare the
application's publish/subscribe interface statically. This is done in an `interface.yml` file. A generator turns
that declaration into C++ glue code, which makes the ordinary, statically typed Goby calls. The
application author writes the `interface.yml` and their Julia or Python code, but does not have to write any C++.

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
C++ applications use, so a Python application accepts the same command line.

For embedding an application in a larger program, or for tests that should not need a command
line, the configuration can be passed directly:

```python
goby.run(PythonDemo, config=PythonDemoConfig(...))     # a protobuf message
goby.run(PythonDemo, config_text="my_value_a: 42")     # Protobuf TextFormat
goby.run(PythonDemo, argv=["python_demo", "-v"])       # an explicit command line
```

An invalid configuration is reported the way a C++ application reports it, and `goby.run()`
returns a non-zero value rather than raising or exiting.

### Signals and the GIL

The Goby event loop runs in C++ with the Python global interpreter lock (GIL) released, and each callback into Python reacquires
it. While the loop is running, Goby installs its own `SIGINT`/`SIGTERM` handlers in place of the
interpreter's, so that Ctrl-C asks the application to quit cleanly rather than raising a
`KeyboardInterrupt` from inside a C++ call stack. An application that is blocked waiting for data
(no loop frequency, nothing arriving) will not notice the request until something wakes it; a
second Ctrl-C restores the default handler and terminates the process.

### Threads

An application launches threads written in Python, mirroring how a C++ application launches
`SimpleThread`s. `Thread` comes from the generated module, alongside the application class:

```python
from python_demo_goby import SingleThreadApplication, Thread, groups


class Reporter(Thread):
    def __init__(self):
        super().__init__(loop_frequency_hertz=1)
        self.interthread().subscribe(groups.status, self.on_status)

    def on_status(self, status) -> None:
        self.interprocess().publish(groups.report, to_report(status))


class PythonDemo(SingleThreadApplication):
    def __init__(self):
        super().__init__(loop_frequency_hertz=10)
        self.launch_thread(Reporter)

        self.interthread().publish(groups.status, {"ready": True})
```

The class is constructed on its own thread, so what it subscribes to in its constructor is
delivered there, and `initialize()`, `loop()` and `finalize()` run there too. `index=` launches
several threads of one class, and `join_thread()` stops one; the rest are stopped when the
application exits.

The interthread layer is implemented in Python rather than through the C++
`InterThreadTransporter`. Nothing crosses the language boundary, so an interthread message is any
Python object rather than only a protobuf message, and an interthread group is any string,
whether or not it is declared in `interface.yml`. The generated C++ has no case for the layer,
which is why an application does not need `MultiThreadApplication` to use it. Every subscriber is
handed the same object, so a message must not be modified after it is published — the rule that
applies to the C++ `shared_ptr` publish.

The outer layers still belong to the C++ application, which lives on the main thread. A
publication or subscription from any other thread is handed to it through a mailbox, which is
what a C++ thread's `InterProcessForwarder` does with its inner interthread transporter, and
messages are delivered back to the thread that subscribed.

```
       main thread                                  launched thread
  +---------------------+       interthread      +---------------------+
  |     application     | <--------------------> |       Thread        |
  +---------------------+   (Python objects)     +---------------------+
            |                                              |
            | interprocess, intermodule                    | mailbox
            v                                              |
  +---------------------+                                  |
  |     C++ portal      | <--------------------------------+
  +---------------------+
```

The application picks that work up in its `loop()`, so its loop rate bounds how long a thread's
interprocess publication waits before it goes out. Launching a thread raises the C++ loop rate to
`interthread_poll_frequency_hertz` (10 Hz by default) when the application asked for a slower one,
without changing how often the application's own `loop()` is called:

```python
super().__init__(loop_frequency_hertz=1, interthread_poll_frequency_hertz=100)
```

Python threads are Python threads: the GIL interleaves them rather than running them in parallel,
so they buy independent loop rates and separation of concerns, not throughput. A thread that
raises prints its traceback and stops the application.

`set_loop_frequency()` changes the rate while the application is running, which is what a
commanded sample-rate change needs:

```python
def on_command(self, command: sensor_pb2.Command) -> None:
    self.set_loop_frequency(command.sample_rate_hertz)
```

Threads have the same method. Zero stops `loop()` being called without stopping the application
or thread receiving messages, and an application whose threads are polled faster than it loops
keeps polling them at `interthread_poll_frequency_hertz`.

### Time and simulation

Under `app { simulation { time { use_sim_time: true warp_factor: 10 } } }` Goby's clocks run ten
times faster than the wall clock, and every rate Goby schedules -- `loop_frequency_hertz`
included -- is in that simulated time. `goby.time` is the Python face of `goby::time`, so an
application that does its own waiting or timestamping stays on the same clock as the rest of the
system:

```python
goby.time.sleep(0.5)      # half a simulated second: 50 ms of wall clock at warp 10
stamp = goby.time.now()   # seconds since the epoch, as SystemClock::now() reports them
```

`goby.time.monotonic()`, `warp_factor()` and `using_sim_time()` are also available. Outside
simulation each is the standard library function, so code written against them needs no branch.
Reading `time.monotonic()` or calling `time.sleep()` directly is the thing to avoid: at warp 10
it runs ten times slower than everything around it, which looks like a sensor fault rather than a
timing bug.

### Logging

`goby.glog` writes to `goby::glog`, so a Python application's output lands in the same place as
every C++ application's, at the verbosity its `app { glog_config { ... } }` asked for:

```python
goby.glog.verbose("connected to the device")
goby.glog.warn(f"no reply in {timeout}s")
```

An application that already uses the standard `logging` module needs no edits beyond installing
the handler:

```python
goby.glog.install()
logging.getLogger(__name__).info("connected to the device")
```

`WARNING` and above map to Goby's `WARN`, `INFO` to `VERBOSE`, and `DEBUG` and below to
`DEBUG1`-`DEBUG3`. Nothing maps to `DIE`, which terminates the application rather than describing
a record; `goby.glog.die()` is there for when that is meant. The logger is configured while the
application is constructed, so writes from before then -- at module import, say -- are dropped.

### Health

Override `health()` to answer `goby_coroner`, mirroring the C++ `health(ThreadHealth&)`:

```python
from goby.middleware.protobuf import coroner_pb2

class Driver(SingleThreadApplication):
    def health(self, health) -> None:
        if not self.device.responding:
            health.state = coroner_pb2.HEALTH__FAILED
            health.error_message = "the I2C device is not answering"
```

`health` arrives with what Goby filled in -- the application name, thread id and
`HEALTH__OK` -- and is reported as given back. It crosses the language boundary as serialized
Protobuf, so an extension this process has no descriptor for survives the round trip and a
project's own `ThreadHealth` extensions can be set from Python.

Threads override the same method. Their reports are collected as children of the application's,
as a C++ `MultiThreadApplication` collects its threads', so a thread appears in the coroner's
report by name. A thread's `health()` runs on the application's thread, so it should read only
what is safe to read from another thread.

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

#### Cross-compiling

Two Pythons are involved and they are not the same one: the generators run at build time against
the *host's* interpreter, while the extension module is loaded by the *target's*, so it is
compiled against the target's headers and named with the target's ABI suffix. Install
`python3-dev` for the target architecture (`python3-dev:arm64`, say) and
`goby_add_python_app()` works under a cross toolchain with nothing else set; it takes the Python
version from the host and the multiarch triplet from `CMAKE_LIBRARY_ARCHITECTURE`.

Where that guess is wrong, each piece can be set explicitly:

| Variable | |
|---|---|
| `GOBY_PYTHON_HOST_EXECUTABLE` | host interpreter that runs the generators |
| `GOBY_PYTHON_TARGET_VERSION` | target Python version, e.g. `3.12` |
| `GOBY_PYTHON_TARGET_INCLUDE_DIRS` | the target's `Python.h` and `pyconfig.h` directories |
| `GOBY_PYTHON_TARGET_SOABI` | extension suffix, e.g. `cpython-312-aarch64-linux-gnu` |

An extension module does not link `libpython` on Unix, so headers and the suffix are all that is
needed. When the target's architecture-dependent `pyconfig.h` is missing the build stops and says
so rather than falling back to the host's, which would produce a module for the wrong ABI.

Note that the extension is architecture-dependent even though the application is Python, so it
belongs in an `Architecture: any` package rather than an `Architecture: all` one.

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

Julia applications can be multi-threaded, on the same model as the Python threads above:
`Goby.run()` takes a list of task modules and runs each on its own Julia thread, and a publication
or subscription on the outer layers from any of them is handed to the task that owns the C++
application. `cxx_channel_check_frequency` takes the place of `interthread_poll_frequency_hertz`,
and unlike Python's threads these run in parallel.

Julia fixes its thread count at startup, so the application has to be launched with one thread per
task module plus three; `goby_add_julia_app()`'s `THREADS` gives that to the generated launcher.
