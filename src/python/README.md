# goby3 — Python support for Goby3

This package provides the ability to write Goby applications in Python. The
[pybind11](https://pybind11.readthedocs.io) library is used to wrap the Goby C++ implementation,
so that the Python implementation stays lightweight and feature-compliant with the C++ one.

A Goby Python application is written as a subclass of the application class generated from its
`interface.yml`, mirroring how a C++ application subclasses `SingleThreadApplication`:

```python
import sys

import goby
from python_demo_goby import SingleThreadApplication, groups
from project.protobuf import comms_pb2


class PythonDemo(SingleThreadApplication):
    def __init__(self):
        super().__init__(loop_frequency_hertz=10)
        self.interprocess().subscribe(groups.modem_rx, comms_pb2.CommsRx, self.on_rx)

    def on_rx(self, msg: comms_pb2.CommsRx) -> None:
        print(f"RECEIVED: {msg}")

    def loop(self) -> None:
        self.interprocess().publish(groups.modem_tx, comms_pb2.CommsTx(request_id=42))


if __name__ == "__main__":
    sys.exit(goby.run(PythonDemo))
```

## Contents

| File | Purpose |
|------|---------|
| `goby/__init__.py` | The public API: `run`, `ApplicationMixin`, `Transporter`, the layer and scheme constants. |
| `goby/_application.py` | The Python side of an application: layer accessors, publish/subscribe, configuration, threads. |
| `goby/_interthread.py` | The interthread layer, implemented in Python, and the threads that use it. |
| `goby/_schemes.py` | Layer and marshalling scheme constants, mirroring the C++ enumerations. |
| `goby/gen.py` | Code generator. Reads `interface.yml` and writes the C++ glue plus the Python module that application code imports. Installed as the `goby_gen_cpp` command. |
| `goby/schema/` | The machine-readable definition of the `interface.yml` format. |
| `tests/` | Unit tests for the generator and the constants; not installed. |

## Threads

Threads are written in Python and launched by the application, mirroring how a C++ application
launches `SimpleThread`s:

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
```

The interthread layer is implemented in Python, so an interthread message is any Python object
and an interthread group is any string. The outer layers belong to the C++ application, which
lives on the main thread; a thread's publications and subscriptions are handed to it and
delivered back. See `doc250_languages.md` for the details and the costs.

## `interface.yml`

Goby3 relies heavily on static analysis (`constexpr` groups, template message types), which
Python cannot provide at import time. The application author therefore declares the application's
publish/subscribe interface in an `interface.yml` file, which the generator turns into C++ glue.
The format is shared exactly with the Julia bindings and is documented in
`share/goby/interface/README.md`.

## Build flow

```
interface.yml
      |
      |  goby_gen_cpp (invoked by goby_add_python_app in CMake)
      |
      +--> PythonDemo.cpp ------- C++ compiler --> _python_demo_goby*.so
      |                           (links goby, goby_zeromq, pybind11)
      |
      +--> python_demo_goby.py -- imported by ------^
                                  application code
```

## Installing

The package is pure Python, so it can be installed straight from this directory:

```
pip install .
```

Building Goby with `-Dbuild_python=ON` installs it as part of `make install`; pass
`-DGOBY_INSTALL_PYTHON_RUNTIME=OFF` when something else (for example `pybuild`, when building the
`python3-goby3` Debian package) is responsible for installing it.
