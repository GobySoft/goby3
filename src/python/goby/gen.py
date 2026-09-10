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

"""Generates the C++ glue code and Python module for a Goby Python application.

Reads the ``interface.yml`` format shared with the Julia bindings; see
``share/goby/interface/README.md`` for the normative description of the format.

Usually invoked from CMake by ``goby_add_python_app()`` rather than by hand::

    goby_gen_cpp interface.yml --cpp-out PythonDemo.cpp --python-out python_demo_goby.py \\
        --module _python_demo_goby --include project/comms.pb.h \\
        --proto-module project.comms_pb2
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence

import yaml

LAYERS = ("interthread", "interprocess", "intermodule")
SUPPORTED_SCHEMES = ("PROTOBUF",)
ENTRY_KEYS = ("group", "scheme", "type")
PORTAL_KEYS = ("alias", "publishes", "subscribes")
APPLICATION_KEYS = ("name", "cpp_type", "config")
CONFIG_KEYS = ("scheme", "type")

# reserved in the shared format but not implemented by any generator yet; called out separately so
# the diagnostic can say "not yet supported" rather than "unknown"
RESERVED_LAYERS = ("intervehicle",)
RESERVED_SCHEMES = ("DCCL", "JSON", "CSTR", "MAVLINK", "CXX_OBJECT")


class InterfaceError(Exception):
    """Raised when an interface.yml file is malformed or uses an unsupported construct."""


@dataclass(frozen=True)
class Entry:
    """One declared publication or subscription."""

    layer: str
    accessor: str
    group: str
    scheme: str
    type: str

    @property
    def layer_enum(self) -> str:
        return self.layer.upper()

    @property
    def group_name(self) -> str:
        """Trailing component of the group expression, e.g. modem_tx."""
        return self.group.replace("::", ".").split(".")[-1]


@dataclass
class Interface:
    """The parsed contents of an interface.yml file."""

    name: str
    cpp_type: str
    config_type: str
    publishes: List[Entry] = field(default_factory=list)
    subscribes: List[Entry] = field(default_factory=list)
    # accessor name -> layer name, in declaration order
    accessors: Dict[str, str] = field(default_factory=dict)

    @property
    def entries(self) -> List[Entry]:
        return self.publishes + self.subscribes

    @property
    def groups(self) -> Dict[str, str]:
        """Unambiguous trailing group name -> full group expression.

        A trailing name shared by two different groups is left out rather than being an error:
        the full group expression always works as a plain string.
        """
        by_name: Dict[str, set] = {}
        for entry in self.entries:
            by_name.setdefault(entry.group_name, set()).add(entry.group)
        return {
            name: next(iter(groups)) for name, groups in by_name.items() if len(groups) == 1
        }

    @property
    def ambiguous_groups(self) -> List[str]:
        by_name: Dict[str, set] = {}
        for entry in self.entries:
            by_name.setdefault(entry.group_name, set()).add(entry.group)
        return sorted(name for name, groups in by_name.items() if len(groups) > 1)


def to_cpp_scoping(name: str) -> str:
    """Turns protobuf/Python scoping ('.') into C++ scoping ('::')."""
    return name if "::" in name else name.replace(".", "::")


def to_proto_scoping(name: str) -> str:
    """Turns C++ scoping ('::') into protobuf/Python scoping ('.')."""
    return name.replace("::", ".")


def to_snake_case(name: str) -> str:
    """CamelCase -> camel_case, for deriving a module name from the application name."""
    # the second pattern splits a run of capitals from the word that follows it, so that
    # HTTPServer becomes http_server rather than httpserver
    split = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    return re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", split).lower()


def _check_keys(mapping, required: Sequence[str], allowed: Sequence[str], where: str) -> None:
    if not isinstance(mapping, dict):
        raise InterfaceError(f"'{where}' must be a mapping, not {type(mapping).__name__}")
    for key in required:
        if key not in mapping:
            raise InterfaceError(f"Interface file must have '{where}.{key}' key")
    for key in mapping:
        if key not in allowed:
            raise InterfaceError(
                f"Unknown key '{where}.{key}'. Supported keys are: {', '.join(allowed)}"
            )


def _check_scheme(scheme, where: str) -> None:
    if scheme in RESERVED_SCHEMES:
        raise InterfaceError(
            f"'{where}' uses scheme '{scheme}', which is reserved in the interface format but "
            f"not yet supported by the Python bindings. Supported schemes: "
            f"{', '.join(SUPPORTED_SCHEMES)}"
        )
    if scheme not in SUPPORTED_SCHEMES:
        raise InterfaceError(
            f"'{where}' uses unknown scheme '{scheme}'. Supported schemes: "
            f"{', '.join(SUPPORTED_SCHEMES)}"
        )


def _collect_portal(layer: str, portal, interface: Interface, where: str) -> None:
    _check_keys(portal, (), PORTAL_KEYS, where)
    accessor = portal.get("alias", layer)
    interface.accessors.setdefault(accessor, layer)

    for kind, destination in (("publishes", interface.publishes), ("subscribes", interface.subscribes)):
        entries = portal.get(kind)
        if entries is None:
            continue
        if not isinstance(entries, list):
            raise InterfaceError(f"'{where}.{kind}' must be a list")
        for entry in entries:
            entry_where = f"{where}.{kind}"
            _check_keys(entry, ENTRY_KEYS, ENTRY_KEYS, entry_where)
            _check_scheme(entry["scheme"], entry_where)
            destination.append(
                Entry(
                    layer=layer,
                    accessor=accessor,
                    group=to_cpp_scoping(str(entry["group"])),
                    scheme=entry["scheme"],
                    type=to_cpp_scoping(str(entry["type"])),
                )
            )


def _check_unambiguous(interface: Interface) -> None:
    """Rejects two entries the generated code could not tell apart.

    A publication or subscription is matched on layer, scheme, type and group. The accessor is
    not part of that, so two entries agreeing on all four compile to two branches with identical
    conditions: the first always wins and the second is unreachable, whichever accessor the
    application called.
    """
    for kind, entries in (("publishes", interface.publishes), ("subscribes", interface.subscribes)):
        seen: Dict[tuple, Entry] = {}
        for entry in entries:
            key = (entry.layer, entry.scheme, entry.type, entry.group)
            first = seen.get(key)
            if first is None:
                seen[key] = entry
                continue
            raise InterfaceError(
                f"'{entry.layer}.{kind}' declares {entry.type} on group {entry.group} "
                f"(scheme {entry.scheme}) twice, as '{first.accessor}' and '{entry.accessor}'. "
                f"Only the layer identifies a portal across the language boundary, so the two "
                f"cannot be told apart and the second would never be reached."
            )


def parse(document) -> Interface:
    """Validates a loaded interface.yml document and returns the Interface it describes."""
    if not isinstance(document, dict):
        raise InterfaceError("Interface file must contain a mapping at the top level")

    for key in document:
        if key in RESERVED_LAYERS:
            raise InterfaceError(
                f"Layer '{key}' is reserved in the interface format but not yet supported by the "
                f"Python bindings. Supported layers: {', '.join(LAYERS)}"
            )
        if key != "application" and key not in LAYERS:
            raise InterfaceError(
                f"Unknown top-level key '{key}' in interface file. Supported keys are: "
                f"application, {', '.join(LAYERS)}"
            )

    if "application" not in document:
        raise InterfaceError("Interface file must have 'application' key")

    application = document["application"]
    _check_keys(application, APPLICATION_KEYS, APPLICATION_KEYS, "application")
    config = application["config"]
    _check_keys(config, CONFIG_KEYS, CONFIG_KEYS, "application.config")
    _check_scheme(config["scheme"], "application.config")

    name = str(application["name"])
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        raise InterfaceError(
            f"'application.name' must be a valid C++ identifier, got '{name}'"
        )

    interface = Interface(
        name=name,
        cpp_type=to_cpp_scoping(str(application["cpp_type"])),
        config_type=to_cpp_scoping(str(config["type"])),
    )

    for layer in LAYERS:
        if layer not in document:
            continue
        value = document[layer]
        if isinstance(value, list):
            for index, portal in enumerate(value):
                _collect_portal(layer, portal, interface, f"{layer}[{index}]")
        else:
            _collect_portal(layer, value, interface, layer)

    _check_unambiguous(interface)

    return interface


def load(path: str) -> Interface:
    """Loads and validates an interface.yml file."""
    with open(path, "r", encoding="utf-8") as handle:
        try:
            document = yaml.safe_load(handle)
        except yaml.YAMLError as error:
            raise InterfaceError(f"Could not parse {path}: {error}") from error
    return parse(document)


def generate_cpp(interface: Interface, includes: Sequence[str], module: str, source: str) -> str:
    """Returns the C++ glue code implementing the declared interface."""
    lines = [
        "// ########################",
        "// #   Goby <--> Python   #",
        "// #  C++ support library #",
        "// ########################",
        "// This file was autogenerated from",
        f"// {source}",
        "",
        "#include <goby/middleware/languages/python/application.h>",
        "",
    ]
    lines += [f'#include "{include}"' for include in includes]
    lines += [
        "",
        f"#define CONFIG_TYPE {interface.config_type}",
        f"#define APPLICATION_TYPE {interface.cpp_type}<CONFIG_TYPE>",
        f"#define APPLICATION_NAME {interface.name}",
        "",
        "class APPLICATION_NAME : public goby::middleware::python::Application<APPLICATION_TYPE>",
        "{",
        "  public:",
        "    using goby::middleware::python::Application<APPLICATION_TYPE>::Application;",
        "",
        "    void publish(goby::middleware::python::Identifier id, const std::string& data)",
        "    {",
    ]
    for entry in interface.publishes:
        lines.append(
            '        GOBY_PYTHON_IF_PUBLICATION({}, {}, {}, {}, "{}", {})'.format(
                entry.scheme,
                entry.layer_enum,
                entry.accessor,
                entry.group,
                entry.group,
                entry.type,
            )
        )
    lines += [
        "",
        '        GOBY_PYTHON_FAIL("publish")',
        "    }",
        "",
        "    void subscribe(goby::middleware::python::Identifier id, pybind11::object callback)",
        "    {",
    ]
    for entry in interface.subscribes:
        lines.append(
            '        GOBY_PYTHON_IF_SUBSCRIPTION({}, {}, {}, {}, "{}", {})'.format(
                entry.scheme,
                entry.layer_enum,
                entry.accessor,
                entry.group,
                entry.group,
                entry.type,
            )
        )
    lines += [
        "",
        '        GOBY_PYTHON_FAIL("subscribe")',
        "    }",
        "};",
        "",
        f"GOBY_PYTHON_DEFINE_MODULE(APPLICATION_NAME, {module})",
        "",
    ]
    return "\n".join(lines)


def generate_python(
    interface: Interface,
    module: str,
    proto_modules: Sequence[str],
    source: str,
) -> str:
    """Returns the Python module that application code imports."""
    base_name = interface.cpp_type.split("::")[-1]

    lines = [
        '"""Goby application interface for {}.'.format(interface.name),
        "",
        "This file was autogenerated from",
        f"    {source}",
        "Do not edit; change the interface file and re-run the generator instead.",
        '"""',
        "",
        "import importlib",
        "",
        "import goby",
        "",
        f"import {module} as _ext",
        "",
        "APPLICATION_NAME = _ext._GOBY_APPLICATION_NAME",
        "",
    ]

    if proto_modules:
        lines += [
            "# importing the generated protobuf modules registers their messages, so that",
            "# message types declared in the interface file can be resolved by name",
            "for _proto_module in (",
        ]
        lines += [f'    "{name}",' for name in proto_modules]
        lines += [
            "):",
            "    importlib.import_module(_proto_module)",
            "",
            'CONFIG_TYPE = goby.message_type("{}")'.format(
                to_proto_scoping(interface.config_type)
            ),
            "",
        ]
    else:
        lines += [
            "# no protobuf modules were passed to the generator, so the configuration type cannot",
            "# be resolved; pass --proto-module to make app.cfg available",
            "CONFIG_TYPE = None",
            "",
        ]

    groups = interface.groups
    if groups:
        lines.append("class groups:")
        lines.append('    """Group names declared in the interface file."""')
        lines.append("")
        for name in sorted(groups):
            lines.append(f'    {name} = "{groups[name]}"')
        lines.append("")
    else:
        lines += ["class groups:", '    """No groups are declared in the interface file."""', "", "    pass", ""]

    for name in interface.ambiguous_groups:
        lines.append(
            f"# '{name}' is not defined above: more than one group shares that trailing name, "
            "so use the full group string"
        )
    if interface.ambiguous_groups:
        lines.append("")

    lines += [
        "",
        f"class {base_name}(goby.ApplicationMixin, _ext._ApplicationBase):",
        f'    """Base class for the {interface.name} Goby application."""',
        "",
        "    _goby_ext = _ext",
        "    _goby_config_type = CONFIG_TYPE",
        "",
    ]

    if interface.accessors:
        for accessor, layer in interface.accessors.items():
            lines += [
                f"    def {accessor}(self):",
                f'        """The {layer} transporter."""',
                f'        return self._goby_transporter("{accessor}", goby.{layer.upper()})',
                "",
            ]
    else:
        lines += ["    pass", ""]

    lines += [
        "",
        f"Application = {base_name}",
        "",
    ]
    return "\n".join(lines)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="goby_gen_cpp",
        description="Generate Goby Python bindings from an interface.yml file",
    )
    parser.add_argument("interface", help="path to the interface.yml file")
    parser.add_argument("--cpp-out", help="path of the C++ glue file to write")
    parser.add_argument("--python-out", help="path of the Python module to write")
    parser.add_argument(
        "--module",
        help="name of the compiled extension module (default: _<application name>_goby)",
    )
    parser.add_argument(
        "--include",
        action="append",
        default=[],
        metavar="HEADER",
        help="additional C++ header to include; repeatable",
    )
    parser.add_argument(
        "--proto-module",
        action="append",
        default=[],
        metavar="MODULE",
        help="generated Python protobuf module (e.g. project.comms_pb2) to import; repeatable",
    )
    parser.add_argument(
        "--print-module-name",
        action="store_true",
        help="print the default extension module name for this interface file and exit",
    )
    args = parser.parse_args(argv)

    try:
        interface = load(args.interface)
    except (InterfaceError, OSError) as error:
        print(f"goby_gen_cpp: {error}", file=sys.stderr)
        return 1

    module = args.module or "_{}_goby".format(to_snake_case(interface.name))

    if args.print_module_name:
        print(module)
        return 0

    if not args.cpp_out and not args.python_out:
        print("goby_gen_cpp: nothing to do: pass --cpp-out and/or --python-out", file=sys.stderr)
        return 1

    source = os.path.abspath(args.interface)

    if args.cpp_out:
        print(f"Generating {args.cpp_out} from {args.interface}")
        with open(args.cpp_out, "w", encoding="utf-8") as handle:
            handle.write(generate_cpp(interface, args.include, module, source))

    if args.python_out:
        print(f"Generating {args.python_out} from {args.interface}")
        with open(args.python_out, "w", encoding="utf-8") as handle:
            handle.write(generate_python(interface, module, args.proto_module, source))

    return 0


if __name__ == "__main__":
    sys.exit(main())
