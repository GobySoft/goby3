"""Unit tests for the interthread layer and the Python threads that use it.

Everything here runs without the compiled extension module: the application base class the
generated module would supply is stood in for by FakeBase, which records what the C++ side would
have been asked to do. That is enough to test the parts that are implemented in Python -- the
interthread bus, the mailbox each thread services, and the forwarding of the outer layers to the
thread that owns the application.

Run with `python3 -m unittest discover -s tests` from src/python, or through CTest as the
goby_test_python_unit test.
"""

import os
import sys
import threading
import time
import types
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import goby  # noqa: E402
from goby import _interthread  # noqa: E402

TIMEOUT = 5


class Message:
    """Stands in for a protobuf message on the layers that marshal one."""

    DESCRIPTOR = types.SimpleNamespace(name="Message")

    def __init__(self, value=b""):
        self.value = value

    def SerializeToString(self):
        return self.value

    def ParseFromString(self, data):
        self.value = data


class OtherMessage:
    DESCRIPTOR = types.SimpleNamespace(name="OtherMessage")

    def __init__(self, value=b""):
        self.value = value


class FakeBase:
    """The compiled _ApplicationBase, reduced to what the Python side calls."""

    def __init__(self, loop_frequency_hertz=0):
        self.loop_frequency_hertz = float(loop_frequency_hertz)
        self.published = []
        self.subscriptions = []
        self.quit_values = []

    def _publish(self, layer, type_name, scheme, group, data):
        self.published.append((int(layer), type_name, int(scheme), group, data))

    def _subscribe(self, layer, type_name, scheme, group, callback):
        self.subscriptions.append((int(layer), type_name, int(scheme), group, callback))

    def _set_loop_frequency_hertz(self, loop_frequency_hertz):
        self.loop_frequency_hertz = float(loop_frequency_hertz)

    def quit(self, return_value=0):
        self.quit_values.append(return_value)

    def app_name(self):
        return "fake_app"


class FakeApp(goby.ApplicationMixin, FakeBase):
    def interprocess(self):
        return self._goby_transporter("interprocess", goby.INTERPROCESS)


class FakeThread(goby.Thread):
    def interprocess(self):
        return self._goby_transporter("interprocess", goby.INTERPROCESS)


class BusTest(unittest.TestCase):
    """The bus itself: what is delivered, to which mailbox, and when it stops being delivered."""

    def setUp(self):
        self.bus = _interthread.Bus()
        self.mailbox = _interthread.Mailbox()
        self.received = []

    def test_delivers_to_the_subscribers_mailbox(self):
        self.bus.subscribe("group", None, self.received.append, self.mailbox)
        self.bus.publish("group", "anything at all")

        # the callback runs when the owning thread services its mailbox, not at publish
        self.assertEqual(self.received, [])
        self.mailbox.service()
        self.assertEqual(self.received, ["anything at all"])

    def test_carries_any_python_object(self):
        self.bus.subscribe("group", None, self.received.append, self.mailbox)
        for message in ({"a": 1}, [1, 2, 3], 42, None, Message(b"x")):
            self.bus.publish("group", message)
        self.mailbox.service()

        self.assertEqual(len(self.received), 5)
        self.assertIs(self.received[0].__class__, dict)
        self.assertIsNone(self.received[3])

    def test_hands_every_subscriber_the_same_object(self):
        other = _interthread.Mailbox()
        elsewhere = []
        self.bus.subscribe("group", None, self.received.append, self.mailbox)
        self.bus.subscribe("group", None, elsewhere.append, other)

        published = {"count": 0}
        self.bus.publish("group", published)
        self.mailbox.service()
        other.service()

        self.assertIs(self.received[0], published)
        self.assertIs(elsewhere[0], published)

    def test_a_subscription_with_a_type_takes_only_that_type(self):
        self.bus.subscribe("group", Message, self.received.append, self.mailbox)
        self.bus.publish("group", OtherMessage(b"no"))
        self.bus.publish("group", "not a message either")
        self.bus.publish("group", Message(b"yes"))
        self.mailbox.service()

        self.assertEqual([message.value for message in self.received], [b"yes"])

    def test_other_groups_are_not_delivered(self):
        self.bus.subscribe("group", None, self.received.append, self.mailbox)
        self.bus.publish("another group", "message")
        self.mailbox.service()

        self.assertEqual(self.received, [])

    def test_publishing_with_no_subscriber_is_not_an_error(self):
        self.bus.publish("nobody is listening", "message")

    def test_removing_a_mailbox_ends_its_subscriptions(self):
        self.bus.subscribe("group", None, self.received.append, self.mailbox)
        self.bus.remove(self.mailbox)
        self.bus.publish("group", "message")
        self.mailbox.service()

        self.assertEqual(self.received, [])
        self.assertEqual(self.bus.groups(), [])


class MailboxTest(unittest.TestCase):
    def setUp(self):
        self.mailbox = _interthread.Mailbox()

    def test_service_runs_everything_queued(self):
        ran = []
        for index in range(3):
            self.mailbox.post(lambda index=index: ran.append(index))
        self.mailbox.service()

        self.assertEqual(ran, [0, 1, 2])

    def test_service_returns_when_there_is_nothing_to_do(self):
        self.mailbox.service()

    def test_shutdown_stops_the_service_loop(self):
        ran = []
        self.mailbox.post(lambda: ran.append("before"))
        self.mailbox.request_shutdown()
        self.mailbox.post(lambda: ran.append("after"))

        with self.assertRaises(_interthread._Shutdown):
            self.mailbox.service()
        self.assertEqual(ran, ["before"])

    def test_a_callback_posting_to_its_own_mailbox_does_not_loop(self):
        ran = []

        def republish():
            ran.append(len(ran))
            self.mailbox.post(republish)

        self.mailbox.post(republish)
        self.mailbox.service()

        # the work it posted is left for the next service, as a C++ poll leaves it for the next
        self.assertEqual(ran, [0])
        self.mailbox.service()
        self.assertEqual(ran, [0, 1])

    def test_service_waits_for_work(self):
        def post_soon():
            time.sleep(0.05)
            self.mailbox.post(lambda: None)

        threading.Thread(target=post_soon, daemon=True).start()
        started = time.monotonic()
        self.mailbox.service(timeout=TIMEOUT)

        self.assertLess(time.monotonic() - started, TIMEOUT)


class ApplicationInterthreadTest(unittest.TestCase):
    """The application's own end of the layer, with no thread launched."""

    def setUp(self):
        self.original_bus = _interthread.bus
        _interthread.bus = _interthread.Bus()
        self.app = FakeApp()

    def tearDown(self):
        _interthread.bus = self.original_bus

    def test_publish_and_subscribe_within_the_application(self):
        received = []
        self.app.interthread().subscribe("group", received.append)
        self.app.interthread().publish("group", {"value": 7})
        self.app.loop()

        self.assertEqual(received, [{"value": 7}])

    def test_interthread_never_reaches_the_cxx_side(self):
        self.app.interthread().subscribe("group", lambda message: None)
        self.app.interthread().publish("group", "message")
        self.app.loop()

        self.assertEqual(self.app.published, [])
        self.assertEqual(self.app.subscriptions, [])

    def test_a_callback_annotation_still_filters_by_type(self):
        received = []

        def on_message(message: Message):
            received.append(message)

        self.app.interthread().subscribe("group", on_message)
        self.app.interthread().publish("group", "not a Message")
        self.app.interthread().publish("group", Message(b"kept"))
        self.app.loop()

        self.assertEqual([message.value for message in received], [b"kept"])

    def test_outer_layers_still_go_straight_to_cxx(self):
        self.app.interprocess().publish("group", Message(b"payload"))

        self.assertEqual(
            self.app.published,
            [(int(goby.INTERPROCESS), "Message", int(goby.PROTOBUF), "group", b"payload")],
        )


class LaunchThreadTest(unittest.TestCase):
    """Threads: construction on their own thread, message flow, and shutdown."""

    def setUp(self):
        self.original_bus = _interthread.bus
        _interthread.bus = _interthread.Bus()

    def tearDown(self):
        _interthread.bus = self.original_bus

    def run_until(self, app, predicate):
        """Stands in for the C++ event loop, which calls loop() at the application's rate."""
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline:
            app.loop()
            if predicate():
                return True
            time.sleep(0.005)
        return False

    def test_a_thread_is_constructed_on_its_own_thread(self):
        app = FakeApp()
        constructed = {}

        class Recorder(goby.Thread):
            def __init__(self):
                super().__init__()
                constructed["thread"] = threading.current_thread()
                constructed["index"] = self.index

        app.launch_thread(Recorder, index=3)
        self.assertTrue(self.run_until(app, lambda: "thread" in constructed))

        self.assertIsNot(constructed["thread"], threading.current_thread())
        self.assertEqual(constructed["index"], 3)
        app.join_thread(Recorder, index=3)

    def test_a_thread_cannot_be_constructed_outside_launch_thread(self):
        with self.assertRaisesRegex(RuntimeError, "launch_thread"):
            goby.Thread()

    def test_round_trip_between_the_application_and_a_thread(self):
        app = FakeApp()
        replies = []

        class Echo(goby.Thread):
            def __init__(self):
                super().__init__()
                self.interthread().subscribe("request", self.on_request)

            def on_request(self, request):
                self.interthread().publish("reply", {"echoed": request})

        app.interthread().subscribe("reply", replies.append)
        app.launch_thread(Echo)

        # the subscription is made on the thread, so wait for it before publishing
        self.assertTrue(self.run_until(app, lambda: "request" in _interthread.bus.groups()))
        app.interthread().publish("request", "hello")

        self.assertTrue(self.run_until(app, lambda: replies))
        self.assertEqual(replies, [{"echoed": "hello"}])
        app.join_thread(Echo)

    def test_a_threads_loop_runs_at_its_own_frequency(self):
        app = FakeApp()
        ticks = []

        class Ticker(goby.Thread):
            def __init__(self):
                super().__init__(loop_frequency_hertz=100)

            def loop(self):
                self.interthread().publish("tick", len(ticks))

        app.interthread().subscribe("tick", ticks.append)
        app.launch_thread(Ticker)

        self.assertTrue(self.run_until(app, lambda: len(ticks) >= 3))
        app.join_thread(Ticker)

    def test_initialize_and_finalize_run_on_the_thread(self):
        app = FakeApp()
        calls = []

        class Lifecycle(goby.Thread):
            def initialize(self):
                calls.append(("initialize", threading.current_thread()))

            def finalize(self):
                calls.append(("finalize", threading.current_thread()))

        app.launch_thread(Lifecycle)
        self.assertTrue(self.run_until(app, lambda: calls))
        app.join_thread(Lifecycle)

        self.assertEqual([call[0] for call in calls], ["initialize", "finalize"])
        self.assertIs(calls[0][1], calls[1][1])
        self.assertIsNot(calls[0][1], threading.current_thread())

    def test_launching_the_same_thread_twice_is_an_error(self):
        app = FakeApp()

        class Once(goby.Thread):
            pass

        app.launch_thread(Once)
        with self.assertRaisesRegex(RuntimeError, "already running"):
            app.launch_thread(Once)

        # an index distinguishes them, as it does in C++
        app.launch_thread(Once, index=1)
        app._goby_join_all_threads()

    def test_launch_thread_rejects_anything_else(self):
        app = FakeApp()
        with self.assertRaisesRegex(TypeError, "goby.Thread"):
            app.launch_thread(threading.Thread)

    def test_a_thread_that_raises_stops_the_application(self):
        app = FakeApp()

        class Doomed(goby.Thread):
            def initialize(self):
                raise ValueError("thread went wrong")

        app.launch_thread(Doomed)
        stderr, sys.stderr = sys.stderr, open(os.devnull, "w")
        try:
            self.assertTrue(self.run_until(app, lambda: app.quit_values))
        finally:
            sys.stderr.close()
            sys.stderr = stderr

        self.assertEqual(app.quit_values, [1])
        app._goby_join_all_threads()

    def test_join_all_threads_stops_everything(self):
        app = FakeApp()

        class Idle(goby.Thread):
            pass

        app.launch_thread(Idle, index=0)
        app.launch_thread(Idle, index=1)
        self.assertTrue(self.run_until(app, lambda: app.running_thread_count() == 2))

        app._goby_join_all_threads()
        self.assertEqual(app.running_thread_count(), 0)


class ThreadForwardingTest(unittest.TestCase):
    """The outer layers belong to the application's thread, so a thread's calls are handed to it."""

    def setUp(self):
        self.original_bus = _interthread.bus
        _interthread.bus = _interthread.Bus()
        self.app = FakeApp()

    def tearDown(self):
        _interthread.bus = self.original_bus
        self.app._goby_join_all_threads()

    def run_until(self, predicate):
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline:
            self.app.loop()
            if predicate():
                return True
            time.sleep(0.005)
        return False

    def test_a_publication_is_made_on_the_applications_thread(self):
        main_thread = threading.current_thread()
        publishing_threads = []

        original_publish = self.app._publish

        def record(*args):
            publishing_threads.append(threading.current_thread())
            original_publish(*args)

        self.app._publish = record

        class Publisher(FakeThread):
            def __init__(self):
                super().__init__()
                self.interprocess().publish("group", Message(b"from a thread"))

        self.app.launch_thread(Publisher)
        self.assertTrue(self.run_until(lambda: self.app.published))

        self.assertEqual(self.app.published[0][3:], ("group", b"from a thread"))
        self.assertEqual(publishing_threads, [main_thread])

    def test_a_subscription_is_registered_on_the_applications_thread(self):
        main_thread = threading.current_thread()
        subscribing_threads = []

        original_subscribe = self.app._subscribe

        def record(*args):
            subscribing_threads.append(threading.current_thread())
            original_subscribe(*args)

        self.app._subscribe = record

        received = []

        class Subscriber(FakeThread):
            def __init__(self):
                super().__init__()
                self.interprocess().subscribe("group", Message, self.on_message)

            def on_message(self, message):
                received.append((message.value, threading.current_thread()))

        self.app.launch_thread(Subscriber)
        self.assertTrue(self.run_until(lambda: self.app.subscriptions))
        self.assertEqual(subscribing_threads, [main_thread])

        # what C++ would deliver, on the application's thread
        self.app.subscriptions[0][4](b"delivered")

        self.assertTrue(self.run_until(lambda: received))
        value, receiving_thread = received[0]
        self.assertEqual(value, b"delivered")
        self.assertIsNot(receiving_thread, main_thread)


class LoopRateTest(unittest.TestCase):
    """Servicing threads raises the C++ loop rate, without changing how often loop() is called."""

    def setUp(self):
        self.original_bus = _interthread.bus
        _interthread.bus = _interthread.Bus()

    def tearDown(self):
        _interthread.bus = self.original_bus

    class Idle(goby.Thread):
        pass

    def test_the_rate_is_untouched_without_threads(self):
        app = FakeApp(loop_frequency_hertz=1)
        self.assertEqual(app.loop_frequency_hertz, 1)

    def test_a_slower_application_is_polled_at_the_poll_rate(self):
        app = FakeApp(loop_frequency_hertz=1, interthread_poll_frequency_hertz=20)
        app.launch_thread(self.Idle)

        self.assertEqual(app.loop_frequency_hertz, 20)
        app._goby_join_all_threads()

    def test_a_faster_application_is_left_alone(self):
        app = FakeApp(loop_frequency_hertz=50, interthread_poll_frequency_hertz=10)
        app.launch_thread(self.Idle)

        self.assertEqual(app.loop_frequency_hertz, 50)
        self.assertTrue(app._goby_loop_due())
        app._goby_join_all_threads()

    def test_the_applications_loop_keeps_its_own_rate(self):
        calls = []

        class Slow(FakeApp):
            def loop(self):
                calls.append(time.monotonic())

        app = Slow(loop_frequency_hertz=10, interthread_poll_frequency_hertz=1000)
        app.launch_thread(self.Idle)

        # the C++ side would now call loop() 1000 times a second; the application asked for 10
        deadline = time.monotonic() + 0.5
        ticks = 0
        while time.monotonic() < deadline:
            app.loop()
            ticks += 1
            time.sleep(0.001)

        self.assertGreater(ticks, len(calls) * 2)
        self.assertGreater(len(calls), 1)
        app._goby_join_all_threads()

    def test_an_application_with_no_loop_is_never_called(self):
        app = FakeApp(loop_frequency_hertz=0, interthread_poll_frequency_hertz=10)
        app.launch_thread(self.Idle)

        self.assertEqual(app.loop_frequency_hertz, 10)
        self.assertFalse(app._goby_loop_due())
        app.loop()
        app._goby_join_all_threads()

    def test_subscribing_alone_is_enough_to_be_serviced(self):
        # an application that only subscribes launches no thread, but still has to pick the
        # message up from its mailbox
        app = FakeApp(loop_frequency_hertz=0, interthread_poll_frequency_hertz=10)
        app.interthread().subscribe("group", lambda message: None)

        self.assertEqual(app.loop_frequency_hertz, 10)

    def test_a_missing_loop_override_is_still_reported(self):
        app = FakeApp(loop_frequency_hertz=10)
        with self.assertRaisesRegex(RuntimeError, "loop\\(\\) must be overridden"):
            app.loop()


if __name__ == "__main__":
    unittest.main(verbosity=2)
