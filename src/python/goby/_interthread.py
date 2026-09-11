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

import functools
import queue
import sys
import threading
import traceback

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


class Bus:
    """Interthread publish/subscribe, shared by every thread of the process."""

    def __init__(self):
        self._lock = threading.Lock()
        self._subscriptions = {}

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
