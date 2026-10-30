# Copyright 2026:
#   GobySoft, LLC (2013-)
#   Community contributors (see AUTHORS file)
# File authors:
#   Toby Schneider <toby@gobysoft.org>
#
#
# This file is part of the Goby Underwater Autonomy Project Libraries
# ("The Goby Libraries").
#
# The Goby Libraries are free software: you can redistribute them and/or modify
# them under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2.1 of the License, or
# (at your option) any later version.
#
# The Goby Libraries are distributed in the hope that they will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with Goby.  If not, see <http://www.gnu.org/licenses/>.

"""Python support for the Goby3 middleware.

A Goby Python application is written as a subclass of the application class generated from its
``interface.yml``, mirroring how a C++ application subclasses ``SingleThreadApplication``::

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

Threads are written in Python too, and launched by the application::

    class Reporter(Thread):
        def __init__(self):
            super().__init__(loop_frequency_hertz=1)
            self.interthread().subscribe(groups.status, self.on_status)

    class PythonDemo(SingleThreadApplication):
        def __init__(self):
            super().__init__(loop_frequency_hertz=10)
            self.launch_thread(Reporter)

The interthread layer is implemented in Python rather than through the C++
``InterThreadTransporter``, so an interthread message is any Python object and an interthread
group is any string.

See ``share/goby/interface/README.md`` for the ``interface.yml`` format, which is shared with the
Julia bindings.
"""

import pkgutil

# Goby's own .proto files declare the "goby" protobuf package, so protoc puts the Python modules
# it generates from them in a goby/ tree of their own (goby.middleware.protobuf.app_config_pb2
# and friends). Extending __path__ the way the "google" namespace does lets those trees live
# alongside this package instead of one shadowing the other.
__path__ = pkgutil.extend_path(__path__, __name__)

from . import glog, time
from ._application import (
    ApplicationMixin,
    ConfigError,
    Transporter,
    message_type,
    run,
)
from ._interthread import Thread
from ._runtime import bind as _bind_extension
from ._schemes import (
    ALL_SCHEMES,
    CSTR,
    CXX_OBJECT,
    DCCL,
    INTERMODULE,
    INTERPROCESS,
    INTERTHREAD,
    JSON,
    MAVLINK,
    NULL_SCHEME,
    PROTOBUF,
    MarshallingScheme,
    PubSubLayer,
)
from ._version import __version__

__all__ = [
    "ALL_SCHEMES",
    "ApplicationMixin",
    "CSTR",
    "CXX_OBJECT",
    "ConfigError",
    "DCCL",
    "INTERMODULE",
    "INTERPROCESS",
    "INTERTHREAD",
    "JSON",
    "MAVLINK",
    "MarshallingScheme",
    "NULL_SCHEME",
    "PROTOBUF",
    "PubSubLayer",
    "Thread",
    "Transporter",
    "__version__",
    "glog",
    "message_type",
    "run",
    "time",
]
