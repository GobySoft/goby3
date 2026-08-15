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
            'project::groups::modem_rx, "project::groups::modem_rx", project::protobuf::CommsRx)',
            self.cpp,
        )

    def test_defines_the_module(self):
        self.assertIn("GOBY_PYTHON_DEFINE_MODULE(APPLICATION_NAME, _python_demo_goby)", self.cpp)

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
                module = gen.generate_python(interface, "_x_goby", [], name)
                compile(module, "x_goby.py", "exec")


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


if __name__ == "__main__":
    unittest.main()
