"""Checks the Python layer and scheme constants against the C++ headers they mirror.

The `goby` package deliberately defines these in Python rather than re-exporting them from a
compiled module, so that it stays architecture-independent (and so `python3-goby3` can be
`Architecture: all`). That only works if something keeps the two definitions in step.
"""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from goby import MarshallingScheme, PubSubLayer  # noqa: E402

SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
LANGUAGES_COMMON = os.path.join(SRC, "middleware", "languages", "common", "interface.h")
MARSHALLING = os.path.join(SRC, "middleware", "marshalling", "interface.h")


def cpp_enumerators(path, enum_name):
    """Returns {name: value} for a C++ enum with explicit values for every enumerator."""
    with open(path, encoding="utf-8") as handle:
        source = handle.read()

    match = re.search(
        r"enum(?:\s+class)?\s+" + re.escape(enum_name) + r"\s*\{(.*?)\}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"Could not find enum {enum_name} in {path}")

    body = re.sub(r"//[^\n]*", "", match.group(1))
    values = {}
    for name, value in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+)", body):
        values[name] = int(value)
    return values


class SchemeConstantsTest(unittest.TestCase):
    def test_pubsub_layer_matches_cpp(self):
        cpp = cpp_enumerators(LANGUAGES_COMMON, "PubSubLayer")
        python = {member.name: int(member) for member in PubSubLayer}
        self.assertEqual(python, cpp)

    def test_marshalling_scheme_matches_cpp(self):
        cpp = cpp_enumerators(MARSHALLING, "MarshallingSchemeEnum")
        python = {member.name: int(member) for member in MarshallingScheme}
        self.assertEqual(python, cpp)


if __name__ == "__main__":
    unittest.main()
