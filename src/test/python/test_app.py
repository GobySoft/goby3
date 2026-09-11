#!/usr/bin/env python3
"""Checks the generate -> compile -> import chain for the Python bindings.

Imports the extension module built from interface.yml and exercises everything that does not
need a running gobyd: the generated module's contents, configuration parsing (including the
failure paths), and the application class's shape. Actually exchanging messages needs a portal
and belongs in the interprocess integration test.
"""

import sys
import unittest

import goby
import goby_python_test_app_goby as app_module
from goby.test.python import test_pb2


class GeneratedModuleTest(unittest.TestCase):
    def test_exports_the_application_name(self):
        self.assertEqual(app_module.APPLICATION_NAME, "GobyPythonTestApp")

    def test_group_constants_come_from_the_interface_file(self):
        self.assertEqual(app_module.groups.tx, "goby::test::python::groups::tx")
        self.assertEqual(app_module.groups.rx, "goby::test::python::groups::rx")

    def test_interthread_groups_need_no_cxx_declaration(self):
        # the layer never crosses into C++, so its groups are strings the application chooses
        # rather than groups declared in a header
        self.assertEqual(app_module.groups.thread_status, "thread_status")
        self.assertEqual(app_module.groups.thread_request, "thread_request")

    def test_resolves_the_config_type(self):
        self.assertIs(app_module.CONFIG_TYPE, test_pb2.TestConfig)

    def test_base_class_is_named_after_the_cpp_type(self):
        self.assertTrue(hasattr(app_module, "SingleThreadApplication"))
        self.assertIs(app_module.Application, app_module.SingleThreadApplication)

    def test_base_class_mixes_in_the_python_api(self):
        base = app_module.SingleThreadApplication
        self.assertTrue(issubclass(base, goby.ApplicationMixin))
        for method in (
            "loop",
            "initialize",
            "finalize",
            "quit",
            "app_name",
            "interprocess",
            "interthread",
            "launch_thread",
            "join_thread",
        ):
            self.assertTrue(callable(getattr(base, method)), method)

    def test_thread_base_class_has_the_same_accessors(self):
        self.assertTrue(issubclass(app_module.Thread, goby.Thread))
        for method in ("loop", "initialize", "finalize", "interprocess", "interthread"):
            self.assertTrue(callable(getattr(app_module.Thread, method)), method)


class InterthreadTest(unittest.TestCase):
    """The interthread layer is pure Python, so it works with no portal and no application."""

    def setUp(self):
        from goby import _interthread

        self.interthread = _interthread
        self.original_bus = _interthread.bus
        _interthread.bus = _interthread.Bus()

    def tearDown(self):
        self.interthread.bus = self.original_bus

    def test_carries_a_protobuf_message_unmarshalled(self):
        received = []
        mailbox = self.interthread.Mailbox()
        published = test_pb2.CommsTx(request_id=42)

        self.interthread.bus.subscribe(
            app_module.groups.thread_status, None, received.append, mailbox
        )
        self.interthread.bus.publish(app_module.groups.thread_status, published)
        mailbox.service()

        # the same object, not a copy parsed back from bytes
        self.assertEqual(len(received), 1)
        self.assertIs(received[0], published)

    def test_carries_anything_else_too(self):
        received = []
        mailbox = self.interthread.Mailbox()

        self.interthread.bus.subscribe("any_group", None, received.append, mailbox)
        self.interthread.bus.publish("any_group", {"not": "a protobuf message"})
        mailbox.service()

        self.assertEqual(received, [{"not": "a protobuf message"}])


class ConfigureTest(unittest.TestCase):
    """The command line handling Python applications inherit from ProtobufConfigurator."""

    def setUp(self):
        self.base = app_module.SingleThreadApplication._goby_ext._ApplicationBase

    def test_reads_a_command_line(self):
        serialized = self.base._configure(["goby_python_test_app", "--num_messages", "7"])
        config = test_pb2.TestConfig()
        config.ParseFromString(serialized)
        self.assertEqual(config.num_messages, 7)

    def test_application_name_defaults_from_argv0(self):
        serialized = self.base._configure(["goby_python_test_app"])
        config = test_pb2.TestConfig()
        config.ParseFromString(serialized)
        self.assertEqual(config.app.name, "goby_python_test_app")
        self.assertEqual(config.app.binary, "goby_python_test_app")

    def test_app_name_can_be_overridden(self):
        serialized = self.base._configure(
            ["goby_python_test_app", "--app_name", "renamed_app"]
        )
        config = test_pb2.TestConfig()
        config.ParseFromString(serialized)
        self.assertEqual(config.app.name, "renamed_app")

    def test_reads_text_format(self):
        serialized = self.base._configure_from_text("num_messages: 3")
        config = test_pb2.TestConfig()
        config.ParseFromString(serialized)
        self.assertEqual(config.num_messages, 3)

    def test_rejects_malformed_text_format(self):
        with self.assertRaises(goby.ConfigError):
            self.base._configure_from_text("this is not a config")

    def test_rejects_an_unknown_field(self):
        with self.assertRaises(goby.ConfigError):
            self.base._configure_from_text("no_such_field: 3")

    def test_reads_a_serialized_message(self):
        source = test_pb2.TestConfig(num_messages=11)
        serialized = self.base._configure_from_serialized(source.SerializeToString())
        config = test_pb2.TestConfig()
        config.ParseFromString(serialized)
        self.assertEqual(config.num_messages, 11)


class RunTest(unittest.TestCase):
    def test_run_rejects_a_class_that_is_not_a_goby_application(self):
        with self.assertRaises(TypeError):
            goby.run(object)

    def test_run_rejects_conflicting_configuration_sources(self):
        with self.assertRaises(TypeError):
            goby.run(
                app_module.SingleThreadApplication,
                config=test_pb2.TestConfig(),
                config_text="num_messages: 1",
            )

    def test_bad_command_line_returns_a_failure_code(self):
        # ProtobufConfigurator reports the error the way it does for a C++ application, and
        # goby.run turns that into a return value rather than an exception or an exit()
        self.assertEqual(
            goby.run(
                app_module.SingleThreadApplication,
                argv=["goby_python_test_app", "--no_such_option"],
            ),
            1,
        )


class SubscribeApiTest(unittest.TestCase):
    """Type inference and validation happen in Python, before any C++ call."""

    def test_infers_the_message_type_from_an_annotation(self):
        def callback(msg: test_pb2.CommsRx) -> None:
            pass

        from goby._application import _callback_message_type

        self.assertIs(_callback_message_type(callback), test_pb2.CommsRx)

    def test_unannotated_callback_is_a_clear_error(self):
        def callback(msg):
            pass

        from goby._application import _callback_message_type

        with self.assertRaisesRegex(TypeError, "Could not infer the message type"):
            _callback_message_type(callback)

    def test_callback_arity_is_checked(self):
        def callback(first, second):
            pass

        from goby._application import _callback_message_type

        with self.assertRaisesRegex(TypeError, "exactly one"):
            _callback_message_type(callback)


class ExtensionBindingTest(unittest.TestCase):
    """goby.time and goby.glog reach C++ through the extension the generated module binds."""

    def test_importing_the_generated_module_binds_the_extension(self):
        from goby import _runtime

        self.assertIs(_runtime.extension(), app_module._ext)

    def test_a_second_extension_is_refused(self):
        from goby import _runtime

        class Other:
            __name__ = "_other_goby"

        with self.assertRaisesRegex(RuntimeError, "one generated module"):
            _runtime.bind(Other())


class SimTimeTest(unittest.TestCase):
    """The warp factor the C++ side read out of the configuration, as goby.time sees it."""

    # goby::middleware::detail::configure_simulation_time() applies simulation settings when the
    # configuration asks for them and otherwise leaves them alone, because an application
    # configures once. So a process that has read one warped configuration stays warped, and the
    # unwarped default is checked in the unit tests rather than here.

    def setUp(self):
        self.base = app_module.SingleThreadApplication._goby_ext._ApplicationBase
        self.addCleanup(goby.time._reset_cache)

    def test_the_configured_warp_factor_reaches_python(self):
        self.base._configure_from_text(
            "app { simulation { time { use_sim_time: true warp_factor: 10 } } }"
        )
        goby.time._reset_cache()

        self.assertTrue(goby.time.using_sim_time())
        self.assertEqual(goby.time.warp_factor(), 10)

    def test_sleep_is_scaled_by_the_warp_factor(self):
        import time as wall

        self.base._configure_from_text(
            "app { simulation { time { use_sim_time: true warp_factor: 20 } } }"
        )
        goby.time._reset_cache()

        started = wall.monotonic()
        goby.time.sleep(1.0)
        slept = wall.monotonic() - started

        # one simulated second at warp 20 is a twentieth of a wall-clock second
        self.assertLess(slept, 0.5)

    def test_monotonic_runs_at_the_configured_warp(self):
        import time as wall

        self.base._configure_from_text(
            "app { simulation { time { use_sim_time: true warp_factor: 10 } } }"
        )
        goby.time._reset_cache()

        started = goby.time.monotonic()
        wall.sleep(0.05)
        elapsed = goby.time.monotonic() - started

        # what schedules loop(): 0.05 wall seconds is half a simulated second at warp 10
        self.assertGreater(elapsed, 0.25)


class GlogTest(unittest.TestCase):
    """The bridge into goby::glog. The logger is configured when an application is built, so
    these check the call reaches C++ rather than what lands in the log."""

    def test_is_enabled_answers_from_cxx(self):
        self.assertIn(goby.glog.is_enabled(goby.glog.WARN), (True, False))

    def test_writing_before_an_application_exists_is_harmless(self):
        goby.glog.warn("no application has been constructed yet")
        goby.glog.verbose("nor here")

    def test_a_group_can_be_declared(self):
        goby.glog.add_group("test_group", "a group declared by the test")
        goby.glog.verbose("into the group", group="test_group")

    def test_logging_records_reach_glog(self):
        import logging

        logger = logging.getLogger("goby.test.python.glog")
        logger.propagate = False
        goby.glog.install(logger)
        try:
            logger.info("through the handler")
            logger.warning("and a warning")
        finally:
            logger.handlers = []


class ApiSurfaceTest(unittest.TestCase):
    """What the generated base class offers. Constructing one needs a portal and a running
    gobyd, so the behaviour behind these is tested against a stand-in base in the unit tests."""

    def test_health_is_overridable_and_wired_to_cxx(self):
        base = app_module.SingleThreadApplication
        self.assertTrue(callable(base.health))
        # the name the C++ trampoline looks up, which is what makes an override reachable
        self.assertTrue(callable(base._goby_health))
        self.assertIn("_goby_health", dir(base._goby_ext._ApplicationBase))

    def test_the_loop_rate_can_be_changed(self):
        base = app_module.SingleThreadApplication
        self.assertTrue(callable(base.set_loop_frequency))
        self.assertIsInstance(base.loop_frequency_hertz, property)
        # the private C++ setter it drives, which threads also share
        self.assertIn("_set_loop_frequency_hertz", dir(base._goby_ext._ApplicationBase))

    def test_threads_have_the_same_two(self):
        self.assertTrue(callable(app_module.Thread.health))
        self.assertTrue(callable(app_module.Thread.set_loop_frequency))


if __name__ == "__main__":
    unittest.main(verbosity=2)
