"""Unit tests for goby.time, the simulation-aware clock.

The warp factor normally comes from the compiled extension, which reads it out of the application
configuration. These tests stand in for the extension so that the arithmetic can be checked
without building one: what matters is that a warped clock runs warp times faster than the wall
clock and that the conversion matches SystemClock::warp() in C++.

Run with `python3 -m unittest discover -s tests` from src/python, or through CTest as the
goby_test_python_unit test.
"""

import os
import sys
import time as wall
import types
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import goby  # noqa: E402
from goby import _runtime  # noqa: E402


class FakeExtension(types.SimpleNamespace):
    """The generated extension, reduced to the simulation settings goby.time reads."""

    def __init__(self, using_sim_time, warp_factor, reference_microtime=0):
        super().__init__(__name__="_fake_goby")
        self._settings = (using_sim_time, warp_factor, reference_microtime)

    def _sim_time(self):
        return self._settings


class SimTimeTest(unittest.TestCase):
    def setUp(self):
        # _runtime.bind() deliberately refuses a second extension, so the module state is
        # replaced directly rather than through the public call
        self.original = _runtime._extension
        self.addCleanup(self._restore)

    def _restore(self):
        _runtime._extension = self.original
        goby.time._reset_cache()

    def _bind(self, extension):
        _runtime._extension = extension
        goby.time._reset_cache()

    def test_no_extension_is_real_time(self):
        self._bind(None)

        self.assertFalse(goby.time.using_sim_time())
        self.assertEqual(goby.time.warp_factor(), 1)

    def test_outside_simulation_nothing_is_warped(self):
        self._bind(FakeExtension(False, 10))

        self.assertFalse(goby.time.using_sim_time())
        self.assertEqual(goby.time.warp_factor(), 1)
        self.assertAlmostEqual(goby.time.now(), wall.time(), delta=1.0)

    def test_warp_factor_is_reported(self):
        self._bind(FakeExtension(True, 10))

        self.assertTrue(goby.time.using_sim_time())
        self.assertEqual(goby.time.warp_factor(), 10)

    def test_a_warp_factor_of_zero_does_not_divide_by_zero(self):
        # warp_factor is an int in C++ and defaults to 1; a configuration setting it to 0 would
        # otherwise make every period infinite
        self._bind(FakeExtension(True, 0))

        self.assertEqual(goby.time.warp_factor(), 1)
        goby.time.sleep(0.001)

    def test_monotonic_runs_warp_times_faster(self):
        self._bind(FakeExtension(True, 10))

        started = goby.time.monotonic()
        wall.sleep(0.05)
        elapsed = goby.time.monotonic() - started

        # 0.05 wall seconds is half a simulated second at warp 10
        self.assertGreater(elapsed, 0.25)

    def test_now_matches_the_cxx_warp_formula(self):
        # t_sim = (t - t0) * w + t0, as SystemClock::warp() computes it
        reference = wall.time() - 100.0
        self._bind(FakeExtension(True, 4, int(reference * 1e6)))

        real = wall.time()
        expected = (real - reference) * 4 + reference

        self.assertAlmostEqual(goby.time.now(), expected, delta=0.5)

    def test_now_is_ahead_of_the_wall_clock_under_warp(self):
        reference = wall.time() - 10.0
        self._bind(FakeExtension(True, 10, int(reference * 1e6)))

        self.assertGreater(goby.time.now(), wall.time())

    def test_sleep_is_divided_by_the_warp_factor(self):
        self._bind(FakeExtension(True, 10))

        started = wall.monotonic()
        goby.time.sleep(0.2)
        slept = wall.monotonic() - started

        # a fifth of a simulated second is a fiftieth of a wall-clock one
        self.assertLess(slept, 0.15)
        self.assertGreater(slept, 0.005)

    def test_sleep_of_zero_returns(self):
        self._bind(FakeExtension(True, 10))
        goby.time.sleep(0)
        goby.time.sleep(-1)

    def test_settings_are_read_once(self):
        extension = FakeExtension(True, 10)
        reads = []
        original = extension._sim_time

        def counting():
            reads.append(None)
            return original()

        extension._sim_time = counting
        self._bind(extension)

        for _ in range(5):
            goby.time.warp_factor()

        self.assertEqual(len(reads), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
