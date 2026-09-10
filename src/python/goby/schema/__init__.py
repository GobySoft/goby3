"""The machine-readable definition of the interface.yml format.

``interface.schema.json`` is the same file installed at ``share/goby/interface`` and used by the
Julia bindings; it is carried here so that the Python package is self-contained when installed
with pip.
"""

import os

SCHEMA_PATH = os.path.join(os.path.dirname(__file__), "interface.schema.json")
