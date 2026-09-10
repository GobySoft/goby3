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


if __name__ == "__main__":
    unittest.main(verbosity=2)
