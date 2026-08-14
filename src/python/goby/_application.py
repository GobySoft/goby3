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

import inspect
import sys
import typing

from ._schemes import PROTOBUF, MarshallingScheme, PubSubLayer


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


def _callback_message_type(callback):
    """Infers the protobuf message type a callback expects, from its type annotation."""
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
    if annotation is None:
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
    """Publishes and subscribes on one layer of one application.

    Returned by the layer accessors on the application class -- ``self.interprocess()`` and
    friends -- mirroring the C++ transporter accessors.
    """

    __slots__ = ("_app", "_name", "_layer")

    def __init__(self, app, name, layer):
        self._app = app
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

        The publication must be declared in the application's ``interface.yml``.

        :param group: the group, e.g. ``groups.modem_tx``
        :param message: a protobuf message
        """
        _check_message_type(type(message))
        self._app._publish(
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

        The subscription must be declared in the application's ``interface.yml``.
        """
        if callback is None:
            callback = message_type_
            if callback is None:
                raise TypeError("subscribe() requires a callback")
            message_type_ = _callback_message_type(callback)

        _check_message_type(message_type_)
        if not callable(callback):
            raise TypeError(f"{callback!r} is not callable")

        def _on_bytes(data):
            message = message_type_()
            message.ParseFromString(data)
            callback(message)

        self._app._subscribe(
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

    def _goby_transporter(self, name, layer):
        cache = getattr(self, "_goby_transporters", None)
        if cache is None:
            cache = {}
            self._goby_transporters = cache
        if name not in cache:
            cache[name] = Transporter(self, name, layer)
        return cache[name]


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
    return application._run()
