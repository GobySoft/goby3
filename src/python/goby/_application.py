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

"""The Python side of a Goby application.

Everything here is pure Python. The compiled code lives in the extension module generated for
each application from its ``interface.yml``, which is why the ``goby`` package itself is
architecture-independent.
"""

import functools
import inspect
import sys
import time
import typing

from . import _interthread
from ._schemes import INTERTHREAD, PROTOBUF


class ConfigError(Exception):
    """Raised when the application configuration is invalid or cannot be read.

    This is the Python face of ``goby::middleware::ConfigException``. Goby has already printed a
    diagnostic by the time it reaches Python, in the same form a C++ application would print.
    """


def message_type(full_name):
    """Returns the protobuf message class registered under ``full_name``.

    The module that generated the message must have been imported first; the module generated
    from ``interface.yml`` does this for the modules passed to the generator.

    :param full_name: fully qualified protobuf message name, e.g. ``project.protobuf.CommsRx``
    """
    from google.protobuf import symbol_database

    try:
        return symbol_database.Default().GetSymbol(full_name)
    except KeyError:
        raise LookupError(
            f"No protobuf message named '{full_name}' has been registered. Make sure the "
            f"generated _pb2 module that defines it is passed to the generator "
            f"(--proto-module) or imported before use."
        ) from None


def _callback_message_type(callback, required=True):
    """Infers the message type a callback expects, from its type annotation.

    ``required`` is false on the interthread layer, where a subscription without a type takes
    everything published on the group.
    """
    try:
        signature = inspect.signature(callback)
    except (TypeError, ValueError):
        raise TypeError(f"Could not inspect the signature of {callback!r}") from None

    parameters = [
        parameter
        for parameter in signature.parameters.values()
        if parameter.kind
        in (inspect.Parameter.POSITIONAL_ONLY, inspect.Parameter.POSITIONAL_OR_KEYWORD)
    ]
    if len(parameters) != 1:
        raise TypeError(
            f"Callback {getattr(callback, '__qualname__', callback)!r} must take exactly one "
            f"argument (the message), but takes {len(parameters)}"
        )

    try:
        hints = typing.get_type_hints(callback)
    except Exception:  # unresolvable annotations shouldn't be fatal, just uninformative
        hints = {}

    annotation = hints.get(parameters[0].name)
    if annotation is None and required:
        raise TypeError(
            f"Could not infer the message type for {getattr(callback, '__qualname__', callback)!r}. "
            f"Annotate its argument with the message type, or pass the type explicitly: "
            f"subscribe(group, MessageType, callback)"
        )
    return annotation


def _check_message_type(message_type_):
    if not hasattr(message_type_, "DESCRIPTOR"):
        raise TypeError(
            f"{message_type_!r} is not a protobuf message type. Only the PROTOBUF marshalling "
            f"scheme is currently supported."
        )


class Transporter:
    """Publishes and subscribes on one layer of one application or thread.

    Returned by the layer accessors -- ``self.interprocess()`` and friends -- mirroring the C++
    transporter accessors.
    """

    __slots__ = ("_owner", "_name", "_layer")

    def __init__(self, owner, name, layer):
        self._owner = owner
        self._name = name
        self._layer = int(layer)

    def __repr__(self):
        return f"<goby.Transporter {self._name}>"

    @property
    def name(self):
        """The accessor name, which is the layer name unless an alias was declared."""
        return self._name

    def publish(self, group, message):
        """Publishes ``message`` to ``group`` on this layer.

        On the interthread layer the message is any Python object and the group is any string.
        On the other layers the message is a protobuf message and the publication must be
        declared in the application's ``interface.yml``.

        :param group: the group, e.g. ``groups.modem_tx``
        :param message: the message to publish
        """
        if self._layer == INTERTHREAD:
            _interthread.bus.publish(str(group), message)
            return

        _check_message_type(type(message))
        self._owner._goby_cxx_publish(
            self._layer,
            message.DESCRIPTOR.name,
            int(PROTOBUF),
            str(group),
            message.SerializeToString(),
        )

    def subscribe(self, group, message_type_=None, callback=None):
        """Subscribes to ``group`` on this layer.

        Mirrors the C++ ``subscribe<group, Type>(callback)``::

            self.interprocess().subscribe(groups.modem_rx, comms_pb2.CommsRx, self.on_rx)

        The message type may also be left out, in which case it is taken from the callback's
        type annotation::

            def on_rx(self, msg: comms_pb2.CommsRx) -> None: ...

            self.interprocess().subscribe(groups.modem_rx, self.on_rx)

        On the interthread layer the type may be omitted entirely, and the subscription then
        takes every object published on the group. On the other layers the subscription must be
        declared in the application's ``interface.yml``.

        The callback runs on the thread that subscribed.
        """
        if callback is None:
            callback = message_type_
            if callback is None:
                raise TypeError("subscribe() requires a callback")
            message_type_ = _callback_message_type(
                callback, required=self._layer != INTERTHREAD
            )

        if not callable(callback):
            raise TypeError(f"{callback!r} is not callable")

        if self._layer == INTERTHREAD:
            self._owner._goby_service_required()
            _interthread.bus.subscribe(
                str(group), message_type_, callback, self._owner._goby_mailbox
            )
            return

        _check_message_type(message_type_)

        def _on_bytes(data):
            message = message_type_()
            message.ParseFromString(data)
            callback(message)

        self._owner._goby_cxx_subscribe(
            self._layer,
            message_type_.DESCRIPTOR.name,
            int(PROTOBUF),
            str(group),
            _on_bytes,
        )


class ApplicationMixin:
    """Adds the Python-side conveniences to the generated application base class.

    Application classes generated from ``interface.yml`` inherit from this and from the compiled
    ``_ApplicationBase``; user code subclasses the generated class.
    """

    # both are set by the generated module
    _goby_ext = None
    _goby_config_type = None

    _goby_has_user_loop = False

    def __init__(self, loop_frequency_hertz=0, interthread_poll_frequency_hertz=10):
        """
        :param loop_frequency_hertz: frequency at which to call loop(); zero means never
        :param interthread_poll_frequency_hertz: lower bound on how often the application picks
            up work from its threads, which bounds the latency of anything they send it
        """
        self._goby_loop_frequency_hertz = float(loop_frequency_hertz)
        self._goby_interthread_poll_frequency_hertz = float(interthread_poll_frequency_hertz)
        self._goby_mailbox = _interthread.Mailbox()
        self._goby_threads = {}
        self._goby_serviced = False
        self._goby_loop_period = None
        self._goby_loop_next = 0.0
        super().__init__(loop_frequency_hertz)

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)

        user_loop = cls.__dict__.get("loop")
        if user_loop is None or getattr(user_loop, "_goby_services_threads", False):
            return

        # the C++ loop runs at whichever is faster, the application's rate or the thread poll
        # rate, so the application's own loop() is called on the schedule it asked for
        @functools.wraps(user_loop)
        def loop(self):
            if getattr(self, "_goby_in_loop", False):
                user_loop(self)
                return
            self._goby_in_loop = True
            try:
                self._goby_service()
                if self._goby_loop_due():
                    user_loop(self)
            finally:
                self._goby_in_loop = False

        loop._goby_services_threads = True
        cls.loop = loop
        cls._goby_has_user_loop = True

    @property
    def cfg(self):
        """The configuration this application was started with, as a protobuf message."""
        cached = getattr(self, "_goby_cfg_cached", None)
        if cached is not None:
            return cached

        if self._goby_config_type is None:
            raise LookupError(
                "The configuration type could not be resolved. Pass the generated protobuf "
                "module to the generator with --proto-module so that goby can decode the "
                "application configuration."
            )

        config = self._goby_config_type()
        config.ParseFromString(self._cfg_serialized())
        self._goby_cfg_cached = config
        return config

    def interthread(self):
        """The interthread transporter."""
        return self._goby_transporter("interthread", INTERTHREAD)

    def loop(self):
        """Override to do work at loop_frequency_hertz."""
        self._goby_service()
        if self._goby_has_user_loop or self._goby_loop_frequency_hertz <= 0:
            return
        # the diagnostic the C++ ApplicationWrapper gives, which servicing threads displaces
        raise RuntimeError("loop() must be overridden when loop_frequency is non-zero")

    def launch_thread(self, thread_class, index=None, *args, **kwargs):
        """Starts ``thread_class`` on a thread of its own, mirroring C++ ``launch_thread()``.

        The class is constructed on the new thread, so anything it subscribes to in its
        constructor is delivered there.

        :param thread_class: a ``goby.Thread`` subclass
        :param index: distinguishes several threads of the same class, as it does in C++
        """
        if not (isinstance(thread_class, type) and issubclass(thread_class, _interthread.Thread)):
            raise TypeError(f"{thread_class!r} is not a goby.Thread subclass")

        key = (thread_class, index)
        running = self._goby_threads.get(key)
        if running is not None and running.is_alive():
            raise RuntimeError(
                f"A thread of type {thread_class.__name__} and index {index} is already running"
            )

        self._goby_service_required()
        runner = _interthread.ThreadRunner(self, thread_class, index, args, kwargs)
        self._goby_threads[key] = runner
        runner.start()

    def join_thread(self, thread_class, index=None, timeout=None):
        """Asks the thread to stop and waits for it, mirroring C++ ``join_thread()``."""
        runner = self._goby_threads.pop((thread_class, index), None)
        if runner is None:
            raise RuntimeError(
                f"No thread of type {thread_class.__name__} and index {index} to join"
            )
        self._goby_stop(runner, timeout)

    def running_thread_count(self):
        return sum(1 for runner in self._goby_threads.values() if runner.is_alive())

    def _goby_transporter(self, name, layer):
        cache = getattr(self, "_goby_transporters", None)
        if cache is None:
            cache = {}
            self._goby_transporters = cache
        if name not in cache:
            cache[name] = Transporter(self, name, layer)
        return cache[name]

    def _goby_cxx_publish(self, layer, type_name, scheme, group, data):
        self._publish(layer, type_name, scheme, group, data)

    def _goby_cxx_subscribe(self, layer, type_name, scheme, group, on_bytes):
        self._subscribe(layer, type_name, scheme, group, on_bytes)

    def _goby_service(self):
        """Runs the work the application's threads have handed it."""
        self._goby_mailbox.service()

    def _goby_loop_due(self):
        period = self._goby_loop_period
        if period is None:
            return True
        if period == 0:
            return False

        now = time.monotonic()
        if now < self._goby_loop_next:
            return False
        self._goby_loop_next = max(now, self._goby_loop_next + period)
        return True

    def _goby_service_required(self):
        """Raises the C++ loop rate so the application picks up its threads' work often enough."""
        if self._goby_serviced:
            return
        self._goby_serviced = True

        poll = self._goby_interthread_poll_frequency_hertz
        if poll <= self._goby_loop_frequency_hertz:
            return

        self._set_loop_frequency_hertz(poll)
        self._goby_loop_period = (
            1.0 / self._goby_loop_frequency_hertz
            if self._goby_loop_frequency_hertz > 0
            else 0.0
        )
        self._goby_loop_next = time.monotonic() + self._goby_loop_period

    def _goby_thread_failed(self, name, formatted_traceback):
        """Called from a thread that stopped; the application is quit from its own thread."""

        def report():
            _interthread.report_failure(name, formatted_traceback)
            self.quit(1)

        self._goby_mailbox.post(report)

    def _goby_join_all_threads(self, timeout=5):
        for runner in list(self._goby_threads.values()):
            self._goby_stop(runner, timeout)
        self._goby_threads.clear()

    @staticmethod
    def _goby_stop(runner, timeout):
        runner.mailbox.request_shutdown()
        runner.join(timeout)
        if runner.is_alive():
            print(
                f"Goby: thread {runner.name} did not stop when asked; abandoning it",
                file=sys.stderr,
            )


def run(application_class, argv=None, config=None, config_text=None):
    """Runs a Goby Python application, mirroring ``goby::run<App>(argc, argv)``.

    Reads the configuration, constructs the application, and runs the Goby event loop until the
    application quits. Blocks until then.

    By default the configuration comes from the command line, exactly as it does for a C++
    application (``--help``, ``--example_config``, ``-v``, per-field overrides, and a
    configuration file). ``--help`` and ``--example_config`` exit the interpreter, as they exit a
    C++ application.

    :param application_class: the application class, a subclass of the generated base class
    :param argv: command line to read; defaults to ``sys.argv``
    :param config: configuration as a protobuf message, instead of reading a command line
    :param config_text: configuration as Protobuf TextFormat, instead of reading a command line
    :return: the application return value, suitable for ``sys.exit()``
    """
    extension = getattr(application_class, "_goby_ext", None)
    if extension is None:
        raise TypeError(
            f"{application_class!r} is not a Goby application class. Subclass the application "
            f"class from the module generated from your interface.yml."
        )

    if config is not None and config_text is not None:
        raise TypeError("pass at most one of config= or config_text=")
    if (config is not None or config_text is not None) and argv is not None:
        raise TypeError("argv= cannot be combined with config= or config_text=")

    base = extension._ApplicationBase

    try:
        if config is not None:
            base._configure_from_serialized(config.SerializeToString())
        elif config_text is not None:
            base._configure_from_text(config_text)
        else:
            base._configure([str(arg) for arg in (sys.argv if argv is None else argv)])
    except ConfigError:
        # Goby has already reported the problem in the same form a C++ application would
        return 1

    application = application_class()
    try:
        return application._run()
    finally:
        application._goby_join_all_threads()
