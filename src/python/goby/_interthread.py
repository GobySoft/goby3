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

"""The interthread layer, and the Python threads that use it.

The C++ interthread layer carries C++ objects between C++ threads. The threads of a Python
application are Python threads, so the layer is implemented here rather than through
``InterThreadTransporter``: nothing crosses the language boundary and nothing is marshalled, so
an interthread message is any Python object and an interthread group is any string.

The outer layers still belong to the C++ application, which lives on the main thread. A
publication or subscription from any other thread is handed to it through that thread's mailbox,
which is what a C++ thread's ``InterProcessForwarder`` does with its inner interthread
transporter.
"""

import builtins
import functools
import importlib
import queue
import sys
import threading
import traceback

from . import _runtime
from . import time as goby_time
from ._schemes import INTERTHREAD

_SHUTDOWN = object()


class _Shutdown(Exception):
    """Raised out of Mailbox.service() when the thread has been asked to stop."""


class Mailbox:
    """The work queue for one thread.

    Every item is a callable that runs on the thread owning the mailbox; that is how a message
    published by another thread reaches the callback that asked for it.
    """

    def __init__(self):
        self._queue = queue.Queue()

    def post(self, work):
        self._queue.put(work)

    def request_shutdown(self):
        self._queue.put(_SHUTDOWN)

    def service(self, timeout=0):
        """Runs the queued work, waiting up to ``timeout`` seconds (``None`` blocks) for the first item."""
        # only the work already queued is run, so that a callback publishing to its own thread
        # is picked up by the next service rather than looping here; a C++ poll takes the same
        # snapshot of its queue before running any callback
        queued = self._queue.qsize()

        try:
            if timeout is None or timeout > 0:
                self._run(self._queue.get(timeout=timeout))
            else:
                self._run(self._queue.get_nowait())
        except queue.Empty:
            return

        for _ in range(max(queued - 1, 0)):
            try:
                self._run(self._queue.get_nowait())
            except queue.Empty:
                return

    @staticmethod
    def _run(work):
        if work is _SHUTDOWN:
            raise _Shutdown()
        work()


class _Subscription:
    __slots__ = ("mailbox", "message_type", "callback")

    def __init__(self, mailbox, message_type, callback):
        self.mailbox = mailbox
        self.message_type = message_type
        self.callback = callback


class InterfaceContractError(Exception):
    """A publication or subscription disagrees with the contract in interface.yml: an interthread
    call it does not declare, or a call from a thread other than the one an entry names."""


def _warn(text):
    if _runtime.extension() is not None:
        from . import glog

        glog.warn(text)
    else:
        print(f"Goby: warning: {text}", file=sys.stderr)


def _resolve_type(name):
    """The class an interface.yml type names: a protobuf message, a builtin, a class in
    ``__main__``, or a class reached by its dotted import path."""
    try:
        from google.protobuf import symbol_database

        return symbol_database.Default().GetSymbol(name)
    except (ImportError, KeyError):
        pass

    parts = name.split(".")
    candidates = [builtins, sys.modules.get("__main__")]
    for split in range(len(parts) - 1, 0, -1):
        try:
            candidates.append((importlib.import_module(".".join(parts[:split])), parts[split:]))
        except ImportError:
            continue

    for candidate in candidates:
        root, path = candidate if isinstance(candidate, tuple) else (candidate, parts)
        value = root
        for part in path:
            value = getattr(value, part, None)
            if value is None:
                break
        if isinstance(value, type):
            return value

    raise InterfaceContractError(
        f"type '{name}' in interface.yml is not defined: define it in the application's main "
        f"module, give its import path, or pass the module that generates a protobuf message to "
        f"the generator with --proto-module"
    )


def _thread_names(owner):
    """The names an interface.yml thread: matches for an application or thread: its class and
    every base class, so that an entry naming a base class covers its subclasses."""
    return {cls.__name__ for cls in type(owner).__mro__}


class Contract:
    """The interthread contract declared in interface.yml, which the generated module hands to
    the bus. Once there is one, every interthread publication and subscription is held to it."""

    def __init__(self, entries):
        self._lock = threading.Lock()
        self._entries = [dict(entry, resolved=None, made=False) for entry in entries]
        self._resolved = False

    def resolve(self):
        """Resolves the type names, which have to wait until the application has defined them."""
        with self._lock:
            if self._resolved:
                return
            for entry in self._entries:
                entry["resolved"] = _resolve_type(entry["type"])
            self._resolved = True

    def _matching(self, kind, group, owner):
        self.resolve()
        on_group = [e for e in self._entries if e["kind"] == kind and e["group"] == group]
        names = _thread_names(owner)
        mine = [e for e in on_group if e["thread"] is None or e["thread"] in names]
        if mine:
            return mine

        verb = "publishes" if kind == "publish" else "subscribes"
        who = type(owner).__name__
        if not on_group:
            raise InterfaceContractError(
                f"{who} {verb} on interthread group '{group}', which interface.yml does not "
                f"declare: add it under interthread: {verb}"
            )
        declared = ", ".join(sorted({e["thread"] for e in on_group if e["thread"]}))
        raise InterfaceContractError(
            f"{who} {verb} on interthread group '{group}', which interface.yml declares only for "
            f"{declared}: give the entry thread: {who}, or leave thread out"
        )

    def check_publish(self, owner, group, message):
        mine = self._matching("publish", group, owner)
        if not any(isinstance(message, e["resolved"]) for e in mine):
            declared = " or ".join(sorted({e["type"] for e in mine}))
            raise InterfaceContractError(
                f"{type(owner).__name__} published a {type(message).__name__} on '{group}', "
                f"which interface.yml declares as {declared}"
            )

    def subscription_type(self, owner, group, message_type):
        """The type to subscribe with: the declared one, which the callback must accept."""
        mine = self._matching("subscribe", group, owner)
        for entry in mine:
            if message_type is None or issubclass(entry["resolved"], message_type):
                with self._lock:
                    entry["made"] = True
                return entry["resolved"]

        declared = " or ".join(sorted({e["type"] for e in mine}))
        raise InterfaceContractError(
            f"a callback taking {getattr(message_type, '__name__', message_type)} cannot receive "
            f"{declared}, which interface.yml declares for the subscription of "
            f"{type(owner).__name__} to '{group}'"
        )

    def report_unmade(self):
        with self._lock:
            unmade = [e for e in self._entries if e["kind"] == "subscribe" and not e["made"]]
        for entry in unmade:
            who = f"thread {entry['thread']}" if entry["thread"] else "any thread"
            _warn(
                f"interface.yml declares a {entry['necessity']} interthread subscription of {who} "
                f"to '{entry['group']}', but nothing made it during startup"
            )


class OuterLayerContract:
    """The thread each outer-layer call is declared for. The generated C++ already rejects a call
    interface.yml does not declare, so this checks only which thread makes a declared one."""

    def __init__(self, entries):
        # the C++ glue matches on the message name without its scope, and so does this
        self._entries = [
            dict(entry, type=entry["type"].replace("::", ".").split(".")[-1]) for entry in entries
        ]

    def check(self, owner, kind, layer, group, type_name):
        on_call = [
            e for e in self._entries
            if e["kind"] == kind and e["layer"] == layer and e["group"] == group
            and e["type"] == type_name
        ]
        names = _thread_names(owner)
        if not on_call or any(e["thread"] is None or e["thread"] in names for e in on_call):
            return
        verb = "publishes" if kind == "publish" else "subscribes to"
        declared = ", ".join(sorted({e["thread"] for e in on_call}))
        raise InterfaceContractError(
            f"{type(owner).__name__} {verb} {type_name} on {layer} group '{group}', which "
            f"interface.yml declares only for {declared}: give the entry thread: "
            f"{type(owner).__name__}, or leave thread out"
        )


class Bus:
    """Interthread publish/subscribe, shared by every thread of the process."""

    def __init__(self):
        self._lock = threading.Lock()
        self._subscriptions = {}
        self.contract = None
        self.outer_layer_contract = None

    def subscribe(self, group, message_type, callback, mailbox):
        with self._lock:
            self._subscriptions.setdefault(group, []).append(
                _Subscription(mailbox, message_type, callback)
            )

    def publish(self, group, message):
        with self._lock:
            subscriptions = list(self._subscriptions.get(group, ()))

        for subscription in subscriptions:
            # a subscription is per type as well as per group, as it is in C++; an interthread
            # subscription that named no type takes everything published on the group
            if subscription.message_type is not None and not isinstance(
                message, subscription.message_type
            ):
                continue
            # every subscriber is handed the same object, so a mutation after publish is visible
            # to all of them, exactly as it is for a C++ shared_ptr publish
            subscription.mailbox.post(functools.partial(subscription.callback, message))

    def remove(self, mailbox):
        with self._lock:
            for group, subscriptions in list(self._subscriptions.items()):
                remaining = [s for s in subscriptions if s.mailbox is not mailbox]
                if remaining:
                    self._subscriptions[group] = remaining
                else:
                    del self._subscriptions[group]

    def groups(self):
        with self._lock:
            return sorted(self._subscriptions)


bus = Bus()


def interthread_contract(entries):
    """Declares the interthread contract. Called by the module generated from interface.yml.

    :param entries: one mapping per declared publication or subscription, with ``kind``
        (``publish`` or ``subscribe``), ``group``, ``type`` (its name), ``thread`` (a class name,
        or None for any thread) and ``necessity``
    """
    bus.contract = Contract(entries)


def outer_layer_contract(entries):
    """Declares which thread makes each outer-layer call. Called by the module generated from
    interface.yml for the entries that give a thread.

    :param entries: one mapping per declared call, with ``kind`` (``publish`` or ``subscribe``),
        ``layer`` (``interprocess`` or ``intermodule``), ``group``, ``type`` and ``thread`` (a
        class name, or None for any thread)
    """
    bus.outer_layer_contract = OuterLayerContract(entries)


class _Construction:
    __slots__ = ("mailbox", "app", "index")

    def __init__(self, mailbox, app, index):
        self.mailbox = mailbox
        self.app = app
        self.index = index


_construction = threading.local()


class Thread:
    """Base class for a Goby thread written in Python.

    Mirrors the C++ ``SimpleThread``: the thread is constructed on its own thread, subscribes in
    its constructor, and has ``initialize()``, ``loop()`` and ``finalize()`` called there too::

        class Reporter(Thread):
            def __init__(self):
                super().__init__(loop_frequency_hertz=1)
                self.interthread().subscribe(groups.status, self.on_status)

            def on_status(self, status):
                self.interprocess().publish(groups.report, to_report(status))

    Started with ``Application.launch_thread(Reporter)``.
    """

    def __init__(self, loop_frequency_hertz=0):
        context = getattr(_construction, "context", None)
        if context is None:
            raise RuntimeError(
                "A goby.Thread must be started with the application's launch_thread(), which "
                "constructs it on the thread it runs on."
            )

        self._goby_mailbox = context.mailbox
        self._goby_app = context.app
        self._goby_index = context.index
        self._goby_loop_frequency_hertz = float(loop_frequency_hertz)
        self._goby_transporters = {}

    @property
    def index(self):
        """The index this thread was launched with, or None."""
        return self._goby_index

    @property
    def cfg(self):
        """The configuration the application was started with, as a protobuf message."""
        return self._goby_app.cfg

    @property
    def loop_frequency_hertz(self):
        """The rate loop() is called at, in simulated time."""
        return self._goby_loop_frequency_hertz

    def set_loop_frequency(self, hertz):
        """Changes the rate loop() is called at, while the thread is running.

        Takes effect on the next loop; zero stops loop() being called without stopping the thread
        receiving messages.
        """
        hertz = float(hertz)
        if hertz < 0:
            raise ValueError(f"loop frequency must not be negative, got {hertz}")
        self._goby_loop_frequency_hertz = hertz

    def health(self, health):
        """Override to report this thread's health, as the C++ ``Thread::health()`` does.

        ``health`` arrives with this thread's name and ``HEALTH__OK``. It is reported as a child
        of the application's health, so goby_coroner sees the thread by name.

        Called on the application's thread rather than this one, so read only what is safe to
        read from another thread.
        """

    def app_name(self):
        return self._goby_app.app_name()

    def interthread(self):
        """The interthread transporter."""
        return self._goby_transporter("interthread", INTERTHREAD)

    def loop(self):
        """Override to do work at loop_frequency_hertz."""

    def initialize(self):
        """Override for work that can't be done in the constructor."""

    def finalize(self):
        """Override for cleanup just before the thread exits."""

    def quit(self, return_value=0):
        """Asks the application to exit cleanly."""
        self._goby_app._goby_mailbox.post(
            functools.partial(self._goby_app.quit, return_value)
        )

    def _goby_service_required(self):
        """The runner always services the mailbox, so nothing has to be arranged."""

    def _goby_transporter(self, name, layer):
        from ._application import Transporter  # deferred: _application imports this module

        if name not in self._goby_transporters:
            self._goby_transporters[name] = Transporter(self, name, layer)
        return self._goby_transporters[name]

    def _goby_cxx_publish(self, layer, type_name, scheme, group, data):
        app = self._goby_app
        app._goby_mailbox.post(
            functools.partial(app._publish, layer, type_name, scheme, group, data)
        )

    def _goby_cxx_subscribe(self, layer, type_name, scheme, group, on_bytes):
        app = self._goby_app
        mailbox = self._goby_mailbox

        def deliver(data):
            mailbox.post(functools.partial(on_bytes, data))

        app._goby_mailbox.post(
            functools.partial(app._subscribe, layer, type_name, scheme, group, deliver)
        )


class ThreadRunner(threading.Thread):
    """Runs one goby.Thread: constructs it, services its mailbox, and calls its loop()."""

    def __init__(self, app, thread_class, index, args, kwargs):
        super().__init__(name=thread_name(thread_class, index), daemon=True)
        self.mailbox = Mailbox()
        self._app = app
        self._thread_class = thread_class
        self._index = index
        self._args = args
        self._kwargs = kwargs
        # read by the application's thread to report health, so it is published only once the
        # thread is fully constructed
        self._thread = None

    def run(self):
        thread = None
        try:
            _construction.context = _Construction(self.mailbox, self._app, self._index)
            try:
                thread = self._thread_class(*self._args, **self._kwargs)
            finally:
                _construction.context = None

            self._thread = thread
            thread.initialize()
            self._service(thread)
        except _Shutdown:
            pass
        except BaseException:
            self._app._goby_thread_failed(self.name, traceback.format_exc())
        finally:
            bus.remove(self.mailbox)
            if thread is not None:
                self._finalize(thread)

    def _service(self, thread):
        # simulated time throughout: the loop frequency is a simulated rate, as it is in C++, so
        # the waits that implement it are converted to wall-clock only where they meet the queue
        warp = goby_time.warp_factor()
        next_loop = None

        while True:
            period = _loop_period(thread)
            if period is None:
                # set_loop_frequency(0) leaves the thread receiving messages, so wait for one
                self.mailbox.service(timeout=None)
                next_loop = None
                continue

            if next_loop is None:
                next_loop = goby_time.monotonic() + period

            wait = max(0.0, next_loop - goby_time.monotonic())
            self.mailbox.service(timeout=wait / warp)

            now = goby_time.monotonic()
            if now >= next_loop:
                next_loop = max(now, next_loop + period)
                thread.loop()

    def fill_health(self, health):
        """Fills in this thread's ThreadHealth, for the application's health report."""
        from goby.middleware.protobuf import coroner_pb2

        health.name = self.name
        health.state = coroner_pb2.HEALTH__OK

        thread = self._thread
        if thread is not None:
            thread.health(health)

    def _finalize(self, thread):
        try:
            thread.finalize()
        except BaseException:
            self._app._goby_thread_failed(self.name, traceback.format_exc())


def _loop_period(thread):
    """The thread's loop period in simulated seconds, or None when loop() is switched off.

    Read each time round the service loop, so set_loop_frequency() takes effect on the next one.
    """
    hertz = thread.loop_frequency_hertz
    return 1.0 / hertz if hertz > 0 else None


def thread_name(thread_class, index):
    name = getattr(thread_class, "__qualname__", thread_class.__name__)
    return name if index is None else "{}/{}".format(name, index)


def report_failure(name, formatted_traceback):
    """Prints what stopped a thread, in the form the C++ side reports an uncaught exception."""
    print(
        "Goby: thread {} stopped and the application cannot continue:\n{}".format(
            name, formatted_traceback
        ),
        file=sys.stderr,
    )
