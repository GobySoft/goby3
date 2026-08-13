"""Checks that goby/_version.py has not drifted from the Goby version in CMakeLists.txt.

pyproject.toml is deliberately not templated by CMake, so that `pip install .` and `pybuild`
work on a plain checkout with no CMake run first. The cost of that choice is that the version is
checked in, so it needs a guard.
"""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from goby import __version__  # noqa: E402

CMAKELISTS = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "CMakeLists.txt")
)


class VersionTest(unittest.TestCase):
    def test_matches_cmake(self):
        if not os.path.exists(CMAKELISTS):
            self.skipTest(f"{CMAKELISTS} not found (not an in-tree checkout)")

        with open(CMAKELISTS, encoding="utf-8") as handle:
            source = handle.read()

        parts = []
        for component in ("MAJOR", "MINOR", "PATCH"):
            match = re.search(
                r'set\(GOBY_VERSION_' + component + r'\s+"([^"]+)"\)', source
            )
            self.assertIsNotNone(match, f"GOBY_VERSION_{component} not found in {CMAKELISTS}")
            parts.append(match.group(1))

        expected = ".".join(parts)
        self.assertEqual(
            __version__,
            expected,
            f"goby/_version.py says {__version__}, CMakeLists.txt says {expected}. "
            f"Update src/python/goby/_version.py to match.",
        )


if __name__ == "__main__":
    unittest.main()
