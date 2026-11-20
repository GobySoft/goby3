"""Unit tests for the interface.yml generator.

Run with `python3 -m unittest discover -s tests` from src/python, or through CTest as the
goby_python_gen test.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from goby import gen  # noqa: E402

CORPUS = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "share", "interface", "test")
)

SINGLE_PORTAL = """
application:
  name: PythonDemo
  cpp_type: goby::zeromq::SingleThreadApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.PythonDemoConfig

interprocess:
  publishes:
    - group: project::groups::modem_tx
      scheme: PROTOBUF
      type: project.protobuf.CommsTx
  subscribes:
    - group: project::groups::modem_rx
      scheme: PROTOBUF
      type: project.protobuf.CommsRx
"""


INTERTHREAD_AND_INTERPROCESS = """
application:
  name: PythonDemo
  cpp_type: goby::zeromq::SingleThreadApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.PythonDemoConfig

interthread:
  publishes:
    - group: project::groups::status
      scheme: PROTOBUF
      type: project.protobuf.Status

interprocess:
  publishes:
    - group: project::groups::modem_tx
      scheme: PROTOBUF
      type: project.protobuf.CommsTx
"""


def parse_yaml(text):
    import yaml

    return gen.parse(yaml.safe_load(text))


class CorpusTest(unittest.TestCase):
    """The conformance corpus shared with the Julia generator."""

    def test_valid_corpus_is_accepted(self):
        directory = os.path.join(CORPUS, "valid")
        names = sorted(os.listdir(directory))
        self.assertTrue(names, "the valid corpus is empty")
        for name in names:
            with self.subTest(case=name):
                gen.load(os.path.join(directory, name))

    def test_invalid_corpus_is_rejected(self):
        for subdirectory in ("invalid", "invalid-semantic"):
            directory = os.path.join(CORPUS, subdirectory)
            names = sorted(os.listdir(directory))
            self.assertTrue(names, f"the {subdirectory} corpus is empty")
            for name in names:
                with self.subTest(case=f"{subdirectory}/{name}"):
                    with self.assertRaises(gen.InterfaceError):
                        gen.load(os.path.join(directory, name))


class ParseTest(unittest.TestCase):
    def test_parses_application_metadata(self):
        interface = parse_yaml(SINGLE_PORTAL)
        self.assertEqual(interface.name, "PythonDemo")
        self.assertEqual(interface.cpp_type, "goby::zeromq::SingleThreadApplication")
        self.assertEqual(interface.config_type, "project::config::protobuf::PythonDemoConfig")

    def test_normalizes_scoping(self):
        interface = parse_yaml(SINGLE_PORTAL)
        self.assertEqual(interface.publishes[0].type, "project::protobuf::CommsTx")
        self.assertEqual(interface.publishes[0].group, "project::groups::modem_tx")

    def test_collects_publishes_and_subscribes(self):
        interface = parse_yaml(SINGLE_PORTAL)
        self.assertEqual(len(interface.publishes), 1)
        self.assertEqual(len(interface.subscribes), 1)
        self.assertEqual(interface.publishes[0].layer_enum, "INTERPROCESS")
        self.assertEqual(interface.subscribes[0].accessor, "interprocess")

    def test_group_constants(self):
        interface = parse_yaml(SINGLE_PORTAL)
        self.assertEqual(
            interface.groups,
            {
                "modem_tx": "project::groups::modem_tx",
                "modem_rx": "project::groups::modem_rx",
            },
        )
        self.assertEqual(interface.ambiguous_groups, [])

    def test_ambiguous_group_names_are_dropped_not_fatal(self):
        interface = parse_yaml(
            SINGLE_PORTAL.replace("project::groups::modem_rx", "other::groups::modem_tx")
        )
        self.assertEqual(interface.groups, {})
        self.assertEqual(interface.ambiguous_groups, ["modem_tx"])

    def test_alias_becomes_the_accessor(self):
        interface = parse_yaml(
            """
application:
  name: MultiPortalDemo
  cpp_type: project::MultiPortalApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.Cfg

interprocess:
  - alias: interprocess_zeromq
    publishes:
      - group: project::groups::a
        scheme: PROTOBUF
        type: project.protobuf.A
  - alias: interprocess_udpm
    subscribes:
      - group: project::groups::b
        scheme: PROTOBUF
        type: project.protobuf.B
"""
        )
        self.assertEqual(
            interface.accessors,
            {"interprocess_zeromq": "interprocess", "interprocess_udpm": "interprocess"},
        )
        self.assertEqual(interface.publishes[0].accessor, "interprocess_zeromq")
        self.assertEqual(interface.subscribes[0].accessor, "interprocess_udpm")

    def test_reserved_layer_says_not_yet_supported(self):
        with self.assertRaisesRegex(gen.InterfaceError, "not yet supported"):
            parse_yaml(
                SINGLE_PORTAL + "\nintervehicle:\n  publishes: []\n"
            )

    def test_reserved_scheme_says_not_yet_supported(self):
        with self.assertRaisesRegex(gen.InterfaceError, "not yet supported"):
            parse_yaml(SINGLE_PORTAL.replace("scheme: PROTOBUF\n      type: project.protobuf.CommsTx",
                                             "scheme: DCCL\n      type: project.protobuf.CommsTx"))

    def test_unknown_entry_key_is_rejected(self):
        with self.assertRaisesRegex(gen.InterfaceError, "Unknown key"):
            parse_yaml(SINGLE_PORTAL + "      required: true\n")

    def test_application_name_must_be_an_identifier(self):
        with self.assertRaisesRegex(gen.InterfaceError, "valid C\\+\\+ identifier"):
            parse_yaml(SINGLE_PORTAL.replace("name: PythonDemo", "name: 'Python Demo'"))


class GenerateCppTest(unittest.TestCase):
    def setUp(self):
        self.interface = parse_yaml(SINGLE_PORTAL)
        self.cpp = gen.generate_cpp(
            self.interface, ["project/comms.pb.h"], "_python_demo_goby", "interface.yml"
        )

    def test_defines_the_application_macros(self):
        self.assertIn("#define CONFIG_TYPE project::config::protobuf::PythonDemoConfig", self.cpp)
        self.assertIn(
            "#define APPLICATION_TYPE goby::zeromq::SingleThreadApplication<CONFIG_TYPE>", self.cpp
        )
        self.assertIn("#define APPLICATION_NAME PythonDemo", self.cpp)

    def test_includes_the_binding_header_and_extra_headers(self):
        self.assertIn("#include <goby/middleware/languages/python/application.h>", self.cpp)
        self.assertIn('#include "project/comms.pb.h"', self.cpp)

    def test_emits_publication_and_subscription_macros(self):
        # the group is passed twice: as the C++ expression for the Goby call, and as a string for
        # matching what Python asked for. The two are not interchangeable -- a group's runtime
        # name is not necessarily its C++ variable name, and Python only knows the latter.
        self.assertIn(
            "GOBY_PYTHON_IF_PUBLICATION(PROTOBUF, INTERPROCESS, interprocess, "
            'project::groups::modem_tx, "project::groups::modem_tx", project::protobuf::CommsTx)',
            self.cpp,
        )
        self.assertIn(
            "GOBY_PYTHON_IF_SUBSCRIPTION(PROTOBUF, INTERPROCESS, interprocess, "
            'project::groups::modem_rx, "project::groups::modem_rx", project::protobuf::CommsRx, '
            "OPTIONAL)",
            self.cpp,
        )

    def test_defines_the_module(self):
        self.assertIn("GOBY_PYTHON_DEFINE_MODULE(APPLICATION_NAME, _python_demo_goby)", self.cpp)

    def test_interthread_is_left_out(self):
        # the layer is implemented in Python and never crosses into C++, so the glue has no case
        # for it -- and needs no interthread() accessor on the application class
        cpp = gen.generate_cpp(
            parse_yaml(INTERTHREAD_AND_INTERPROCESS), [], "_python_demo_goby", "interface.yml"
        )
        self.assertNotIn("INTERTHREAD", cpp)
        self.assertNotIn("project::groups::status", cpp)
        self.assertIn("INTERPROCESS", cpp)

    def test_macro_parameter_names_match_the_generated_signatures(self):
        # the macros refer to 'data' and 'callback' by name
        self.assertIn(
            "void publish(goby::middleware::python::Identifier id, const std::string& data)",
            self.cpp,
        )
        self.assertIn(
            "void subscribe(goby::middleware::python::Identifier id, pybind11::object callback)",
            self.cpp,
        )


class GeneratePythonTest(unittest.TestCase):
    def setUp(self):
        self.interface = parse_yaml(SINGLE_PORTAL)
        self.module = gen.generate_python(
            self.interface,
            "_python_demo_goby",
            ["project.comms_pb2", "project.config.python_demo_pb2"],
            "interface.yml",
        )

    def test_is_valid_python(self):
        compile(self.module, "python_demo_goby.py", "exec")

    def test_names_the_base_class_after_the_cpp_type(self):
        self.assertIn(
            "class SingleThreadApplication(goby.ApplicationMixin, _ext._ApplicationBase):",
            self.module,
        )
        self.assertIn("Application = SingleThreadApplication", self.module)

    def test_emits_group_constants(self):
        self.assertIn('modem_tx = "project::groups::modem_tx"', self.module)
        self.assertIn('modem_rx = "project::groups::modem_rx"', self.module)

    def test_emits_layer_accessors(self):
        self.assertIn("def interprocess(self):", self.module)
        self.assertIn('self._goby_transporter("interprocess", goby.INTERPROCESS)', self.module)

    def test_emits_a_thread_base_class(self):
        self.assertIn("class Thread(goby.Thread):", self.module)
        # the same accessors as the application, so a thread publishes the same way it does
        self.assertEqual(self.module.count("def interprocess(self):"), 2)

    def test_interthread_groups_are_still_named(self):
        module = gen.generate_python(
            parse_yaml(INTERTHREAD_AND_INTERPROCESS), "_python_demo_goby", [], "interface.yml"
        )
        compile(module, "python_demo_goby.py", "exec")
        self.assertIn('status = "project::groups::status"', module)
        self.assertIn('self._goby_transporter("interthread", goby.INTERTHREAD)', module)

    def test_resolves_the_config_type(self):
        self.assertIn(
            'CONFIG_TYPE = goby.message_type("project.config.protobuf.PythonDemoConfig")',
            self.module,
        )
        self.assertIn('"project.comms_pb2",', self.module)

    def test_without_proto_modules_the_config_type_is_unresolved(self):
        module = gen.generate_python(self.interface, "_python_demo_goby", [], "interface.yml")
        compile(module, "python_demo_goby.py", "exec")
        self.assertIn("CONFIG_TYPE = None", module)

    def test_aliases_become_accessors(self):
        interface = parse_yaml(
            """
application:
  name: MultiPortalDemo
  cpp_type: project::MultiPortalApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.Cfg

interprocess:
  - alias: interprocess_udpm
    publishes:
      - group: project::groups::a
        scheme: PROTOBUF
        type: project.protobuf.A
"""
        )
        module = gen.generate_python(interface, "_multi_portal_demo_goby", [], "interface.yml")
        compile(module, "multi_portal_demo_goby.py", "exec")
        self.assertIn("def interprocess_udpm(self):", module)
        self.assertIn(
            'self._goby_transporter("interprocess_udpm", goby.INTERPROCESS)', module
        )

    def test_generated_module_for_every_valid_corpus_case_is_valid_python(self):
        directory = os.path.join(CORPUS, "valid")
        for name in sorted(os.listdir(directory)):
            with self.subTest(case=name):
                interface = gen.load(os.path.join(directory, name))
                # the format allows interthread queue settings that only Julia implements
                if any(entry.delivery for _, entry in interface.interthread_entries):
                    with self.assertRaises(gen.InterfaceError):
                        gen.generate_python(interface, "_x_goby", [], name)
                    continue
                module = gen.generate_python(interface, "_x_goby", [], name)
                compile(module, "x_goby.py", "exec")


INTERTHREAD_CONTRACT = """
application:
  name: PythonDemo
  cpp_type: goby::zeromq::SingleThreadApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.PythonDemoConfig

interthread:
  publishes:
    - group: nav
      scheme: PROTOBUF
      type: project.protobuf.NavigationReport
      thread: NavPublisher
  subscribes:
    - group: nav
      scheme: PROTOBUF
      type: project::protobuf::NavigationReport
      thread: DepthWatcher
      necessity: required
    - group: depth_alert
      scheme: NATIVE
      type: DepthAlert
"""


class InterthreadContractTest(unittest.TestCase):
    """interthread: declared in interface.yml becomes a contract the bus enforces, and a manifest
    that goby_clang_tool -gen adds to the application's interface file."""

    def setUp(self):
        self.interface = parse_yaml(INTERTHREAD_CONTRACT)

    def test_threads_and_native_types_are_parsed(self):
        self.assertTrue(self.interface.declares_interthread)
        kinds = [(kind, entry.group, entry.thread) for kind, entry in self.interface.interthread_entries]
        self.assertEqual(
            kinds,
            [
                ("publish", "nav", "NavPublisher"),
                ("subscribe", "nav", "DepthWatcher"),
                ("subscribe", "depth_alert", ""),
            ],
        )
        self.assertEqual(self.interface.subscribes[1].scheme, "NATIVE")

    def test_without_the_section_there_is_no_contract(self):
        interface = parse_yaml(SINGLE_PORTAL)
        self.assertFalse(interface.declares_interthread)
        module = gen.generate_python(interface, "_python_demo_goby", [], "interface.yml")
        self.assertNotIn("interthread_contract", module)

    def test_generated_module_declares_the_contract(self):
        module = gen.generate_python(self.interface, "_python_demo_goby", [], "interface.yml")
        compile(module, "python_demo_goby.py", "exec")
        self.assertIn("goby.interthread_contract([", module)
        # types are written as Python names them, whichever scoping interface.yml used
        self.assertIn(
            '{"kind": "subscribe", "group": "nav", "type": "project.protobuf.NavigationReport", '
            '"thread": \'DepthWatcher\', "necessity": "required"},',
            module,
        )
        self.assertIn('"group": "depth_alert", "type": "DepthAlert", "thread": None', module)

    def test_manifest(self):
        import yaml

        manifest = yaml.safe_load(gen.generate_manifest(self.interface, "demo_target", "x.yml"))
        self.assertEqual(manifest["application"], "demo_target")
        section = manifest["python_interthread"]
        self.assertEqual(
            section["publishes"],
            [{"thread": "NavPublisher", "group": "nav", "type": "project.protobuf.NavigationReport"}],
        )
        self.assertEqual(
            section["subscribes"][1],
            {"group": "depth_alert", "type": "DepthAlert", "necessity": "optional"},
        )

    def test_manifest_without_the_section_names_only_the_application(self):
        manifest = gen.generate_manifest(parse_yaml(SINGLE_PORTAL), "demo_target", "x.yml")
        self.assertNotIn("python_interthread", manifest)
        self.assertIn('application: "demo_target"', manifest)

    def test_queue_settings_are_refused_when_generating(self):
        interface = parse_yaml(
            INTERTHREAD_CONTRACT.replace("necessity: required", "necessity: required\n      capacity: 5")
        )
        for generate in (
            lambda: gen.generate_python(interface, "_x_goby", [], "x.yml"),
            lambda: gen.generate_cpp(interface, [], "_x_goby", "x.yml"),
        ):
            with self.assertRaisesRegex(gen.InterfaceError, "capacity"):
                generate()

    def test_native_is_interthread_only(self):
        with self.assertRaisesRegex(gen.InterfaceError, "NATIVE"):
            parse_yaml(SINGLE_PORTAL.replace("scheme: PROTOBUF\n      type: project.protobuf.CommsTx",
                                             "scheme: NATIVE\n      type: CommsTx"))

    def test_one_thread_may_not_declare_an_entry_twice_but_two_threads_may(self):
        twice = INTERTHREAD_CONTRACT + """    - group: depth_alert
      scheme: NATIVE
      type: DepthAlert
"""
        with self.assertRaises(gen.InterfaceError):
            parse_yaml(twice)
        # depth_alert is declared for any thread, which already covers Logger
        with self.assertRaisesRegex(gen.InterfaceError, "covers every thread"):
            parse_yaml(INTERTHREAD_CONTRACT + """    - group: depth_alert
      scheme: NATIVE
      type: DepthAlert
      thread: Logger
""")
        parse_yaml(INTERTHREAD_CONTRACT + """    - group: nav
      scheme: PROTOBUF
      type: project.protobuf.NavigationReport
      thread: Logger
""")


OUTER_THREADS = """
application:
  name: PythonDemo
  cpp_type: goby::zeromq::SingleThreadApplication
  config:
    scheme: PROTOBUF
    type: project.config.protobuf.PythonDemoConfig

interprocess:
  publishes:
    - group: project::groups::nav
      scheme: PROTOBUF
      type: project.protobuf.NavigationReport
      thread: Navigator
    - group: project::groups::status
      scheme: PROTOBUF
      type: project.protobuf.Status
  subscribes:
    - group: project::groups::command
      scheme: PROTOBUF
      type: project.protobuf.Command
      thread: Navigator
      necessity: required
    - group: project::groups::command
      scheme: PROTOBUF
      type: project.protobuf.Command
      thread: Logger
"""


class OuterLayerThreadTest(unittest.TestCase):
    """thread: on an interprocess or intermodule entry: the C++ handles the call once, the bus
    checks which thread makes it, and goby_clang_tool draws it on that thread."""

    def setUp(self):
        self.interface = parse_yaml(OUTER_THREADS)

    def test_the_cpp_has_one_branch_per_call_with_the_strongest_necessity(self):
        cpp = gen.generate_cpp(self.interface, [], "_python_demo_goby", "interface.yml")
        self.assertEqual(cpp.count("GOBY_PYTHON_IF_SUBSCRIPTION("), 1)
        self.assertIn("project::protobuf::Command, REQUIRED)", cpp)
        self.assertEqual(cpp.count("GOBY_PYTHON_IF_PUBLICATION("), 2)

    def test_the_generated_module_declares_only_threaded_calls(self):
        module = gen.generate_python(self.interface, "_python_demo_goby", [], "interface.yml")
        compile(module, "python_demo_goby.py", "exec")
        self.assertIn("goby.outer_layer_contract([", module)
        self.assertIn('"group": "project::groups::nav", "type": "project::protobuf::NavigationReport", '
                      '"thread": \'Navigator\'}', module)
        # status names no thread, so any thread may publish it and nothing is checked
        self.assertNotIn("project::groups::status", module.split("outer_layer_contract")[1])

    def test_manifest(self):
        import yaml

        manifest = yaml.safe_load(gen.generate_manifest(self.interface, "demo", "x.yml"))
        self.assertNotIn("python_interthread", manifest)
        self.assertEqual(
            manifest["outer_threads"],
            [
                {"layer": "interprocess", "direction": "publish", "thread": "Navigator",
                 "group": "project::groups::nav", "type": "project::protobuf::NavigationReport"},
                {"layer": "interprocess", "direction": "subscribe", "thread": "Navigator",
                 "group": "project::groups::command", "type": "project::protobuf::Command",
                 "necessity": "required"},
                {"layer": "interprocess", "direction": "subscribe", "thread": "Logger",
                 "group": "project::groups::command", "type": "project::protobuf::Command",
                 "necessity": "optional"},
            ],
        )


class ModuleNameTest(unittest.TestCase):
    def test_camel_case_becomes_snake_case(self):
        self.assertEqual(gen.to_snake_case("PythonDemo"), "python_demo")
        self.assertEqual(gen.to_snake_case("HTTPServer"), "http_server")
        self.assertEqual(gen.to_snake_case("Demo"), "demo")


class SchemaTest(unittest.TestCase):
    """The schema is documentation unless something checks it against the corpus."""

    def test_schema_matches_the_corpus(self):
        try:
            import jsonschema
        except ImportError:
            self.skipTest("jsonschema is not installed")

        import json

        import yaml

        from goby.schema import SCHEMA_PATH

        with open(SCHEMA_PATH, encoding="utf-8") as handle:
            schema = json.load(handle)

        validator = jsonschema.Draft202012Validator(schema)

        for name in sorted(os.listdir(os.path.join(CORPUS, "valid"))):
            with self.subTest(valid=name):
                with open(os.path.join(CORPUS, "valid", name), encoding="utf-8") as handle:
                    validator.validate(yaml.safe_load(handle))

        for name in sorted(os.listdir(os.path.join(CORPUS, "invalid"))):
            with self.subTest(invalid=name):
                with open(os.path.join(CORPUS, "invalid", name), encoding="utf-8") as handle:
                    document = yaml.safe_load(handle)
                with self.assertRaises(jsonschema.ValidationError):
                    validator.validate(document)

        # the schema describes the shape of a file, and these are well shaped -- what is wrong
        # with them is a relationship between two entries, which JSON Schema cannot express.
        # They are the generators' to reject, and this records where the schema stops.
        for name in sorted(os.listdir(os.path.join(CORPUS, "invalid-semantic"))):
            with self.subTest(invalid_semantic=name):
                with open(os.path.join(CORPUS, "invalid-semantic", name), encoding="utf-8") as handle:
                    validator.validate(yaml.safe_load(handle))


class MainTest(unittest.TestCase):
    """goby_gen_cpp runs from a build, so it prints nothing unless asked to."""

    def run_main(self, *extra):
        import contextlib
        import io
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            interface = os.path.join(directory, "interface.yml")
            with open(interface, "w", encoding="utf-8") as handle:
                handle.write(SINGLE_PORTAL)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                result = gen.main(
                    [
                        interface,
                        "--cpp-out",
                        os.path.join(directory, "out.cpp"),
                        "--python-out",
                        os.path.join(directory, "out.py"),
                        *extra,
                    ]
                )
            self.assertEqual(result, 0)
            self.assertTrue(os.path.exists(os.path.join(directory, "out.cpp")))
            return out.getvalue()

    def test_quiet_by_default(self):
        self.assertEqual(self.run_main(), "")

    def test_verbose_names_each_file(self):
        output = self.run_main("--verbose")
        self.assertIn("out.cpp from", output)
        self.assertIn("out.py from", output)


if __name__ == "__main__":
    unittest.main()
