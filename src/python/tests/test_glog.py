"""Unit tests for goby.glog, the bridge to the Goby logger.

goby::glog itself is C++, so these tests stand in for the extension and check what would be
handed to it: the verbosity a record maps onto, and that a handler never raises into the code
that logged. The end-to-end path -- that this actually reaches the Goby log file -- is covered by
the compiled test application.

Run with `python3 -m unittest discover -s tests` from src/python, or through CTest as the
goby_test_python_unit test.
"""

import logging
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import goby  # noqa: E402
from goby import _runtime  # noqa: E402


class FakeExtension:
    """The generated extension, reduced to the glog entry points."""

    __name__ = "_fake_goby"

    def __init__(self, enabled_at=goby.glog.DEBUG3):
        self.written = []
        self.groups = []
        self.enabled_at = enabled_at

    def _glog_write(self, verbosity, group, text):
        self.written.append((verbosity, group, text))

    def _glog_is(self, verbosity):
        return verbosity <= self.enabled_at

    def _glog_add_group(self, name, description):
        self.groups.append((name, description))


class GlogTestCase(unittest.TestCase):
    def setUp(self):
        self.original = _runtime._extension
        self.extension = FakeExtension()
        _runtime._extension = self.extension
        self.addCleanup(self._restore)

    def _restore(self):
        _runtime._extension = self.original


class VerbosityMappingTest(unittest.TestCase):
    def test_warning_and_above_are_warnings(self):
        for level in (logging.WARNING, logging.ERROR, logging.CRITICAL):
            self.assertEqual(goby.glog.verbosity_for(level), goby.glog.WARN)

    def test_info_is_the_default_verbosity(self):
        self.assertEqual(goby.glog.verbosity_for(logging.INFO), goby.glog.VERBOSE)

    def test_debug_is_the_first_debug_level(self):
        self.assertEqual(goby.glog.verbosity_for(logging.DEBUG), goby.glog.DEBUG1)

    def test_below_debug_asks_for_more_not_less(self):
        self.assertEqual(goby.glog.verbosity_for(logging.DEBUG - 1), goby.glog.DEBUG2)
        self.assertEqual(goby.glog.verbosity_for(1), goby.glog.DEBUG3)

    def test_nothing_maps_to_die(self):
        # writing at DIE terminates the application, which no log record should be able to do
        for level in range(0, 60):
            self.assertNotEqual(goby.glog.verbosity_for(level), goby.glog.DIE)


class WriteTest(GlogTestCase):
    def test_each_level_writes_at_its_verbosity(self):
        goby.glog.warn("w")
        goby.glog.verbose("v")
        goby.glog.debug1("d1")
        goby.glog.debug2("d2")
        goby.glog.debug3("d3")

        self.assertEqual(
            [(verbosity, text) for verbosity, _, text in self.extension.written],
            [
                (goby.glog.WARN, "w"),
                (goby.glog.VERBOSE, "v"),
                (goby.glog.DEBUG1, "d1"),
                (goby.glog.DEBUG2, "d2"),
                (goby.glog.DEBUG3, "d3"),
            ],
        )

    def test_a_group_is_passed_through(self):
        goby.glog.verbose("text", group="driver")

        self.assertEqual(self.extension.written, [(goby.glog.VERBOSE, "driver", "text")])

    def test_is_enabled_asks_the_logger(self):
        self.extension.enabled_at = goby.glog.WARN

        self.assertTrue(goby.glog.is_enabled(goby.glog.WARN))
        self.assertFalse(goby.glog.is_enabled(goby.glog.DEBUG1))

    def test_add_group_is_declared(self):
        goby.glog.add_group("driver", "the sensor driver")

        self.assertEqual(self.extension.groups, [("driver", "the sensor driver")])


class UnboundTest(unittest.TestCase):
    """Before a generated module is imported there is nothing to write to."""

    def setUp(self):
        self.original = _runtime._extension
        _runtime._extension = None
        self.addCleanup(self._restore)

    def _restore(self):
        _runtime._extension = self.original

    def test_writing_is_dropped_rather_than_raising(self):
        # module-level logging happens before the application is constructed; it should not be
        # the thing that stops the application starting
        goby.glog.warn("nowhere to go")
        self.assertFalse(goby.glog.is_enabled(goby.glog.WARN))

    def test_add_group_explains_what_is_missing(self):
        with self.assertRaisesRegex(RuntimeError, "interface.yml"):
            goby.glog.add_group("driver")


class HandlerTest(GlogTestCase):
    def setUp(self):
        super().setUp()
        self.logger = logging.getLogger("goby.tests.handler")
        self.logger.handlers = []
        self.logger.propagate = False

    def test_records_reach_glog_at_the_mapped_verbosity(self):
        goby.glog.install(self.logger)

        self.logger.info("connected")
        self.logger.warning("no reply")

        self.assertEqual(
            [(verbosity, text) for verbosity, _, text in self.extension.written],
            [(goby.glog.VERBOSE, "connected"), (goby.glog.WARN, "no reply")],
        )

    def test_formatting_is_applied(self):
        goby.glog.install(self.logger)

        self.logger.info("depth %.1f m", 12.34)

        self.assertEqual(self.extension.written[0][2], "depth 12.3 m")

    def test_install_replaces_existing_handlers(self):
        self.logger.addHandler(logging.StreamHandler())
        goby.glog.install(self.logger)

        self.assertEqual(len(self.logger.handlers), 1)
        self.assertIsInstance(self.logger.handlers[0], goby.glog.GlogHandler)

    def test_install_can_keep_existing_handlers(self):
        existing = logging.StreamHandler()
        self.logger.addHandler(existing)
        goby.glog.install(self.logger, replace=False)

        self.assertIn(existing, self.logger.handlers)
        self.assertEqual(len(self.logger.handlers), 2)

    def test_a_group_is_carried_by_the_handler(self):
        goby.glog.install(self.logger, group="driver")

        self.logger.info("text")

        self.assertEqual(self.extension.written[0][1], "driver")

    def test_a_failing_write_does_not_raise_into_the_caller(self):
        def explode(*args):
            raise RuntimeError("glog is unhappy")

        self.extension._glog_write = explode
        handler = goby.glog.install(self.logger)
        handler.handleError = lambda record: None

        self.logger.info("this must not propagate")


if __name__ == "__main__":
    unittest.main(verbosity=2)
