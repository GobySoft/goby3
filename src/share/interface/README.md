# `interface.yml` — Goby language binding interface definition

This directory holds the normative definition of the `interface.yml` file used by all the Goby
language bindings. The format is shared exactly: the same file drives the Julia generator
(`gen_goby.jl`, in `src/share/Goby.jl`) and the Python generator (`goby.gen`, in `src/python`).

| File | Purpose |
|------|---------|
| `README.md` | This document — the normative description of the format. |
| `interface.schema.json` | Machine-readable [JSON Schema](https://json-schema.org) for the same format (YAML is JSON-compatible, so the schema applies directly). |
| `test/valid/*.yml` | Conformance corpus: every generator must accept these. |
| `test/invalid/*.yml` | Conformance corpus: every generator must reject these. |

## Why this file exists

Goby3 relies heavily on static analysis: groups are `constexpr`, message types and marshalling
schemes are template parameters, and the transporter to use is chosen by which accessor you call.
Dynamic languages have none of that at compile time.

Rather than give up the static guarantees, the bindings ask the application author to declare the
publish/subscribe interface up front, in this file. A generator turns the declaration into C++ glue
code that makes the ordinary, statically typed Goby calls. The application author writes the
`interface.yml` and their Julia or Python code — never any C++.

At runtime, a publication or subscription that was not declared here fails with a message naming
the offending layer, group, type and scheme.

> **Not to be confused with** `<target>_interface.yml`, which `goby_clang_tool` *generates* by
> analyzing existing C++ source for the interface visualization tools. This file is written by
> hand and is an input, not an output.

## Top-level keys

| Key | Required | Description |
|-----|----------|-------------|
| `application` | **Yes** | Application metadata (see below). |
| `interthread` | No | Interthread-layer publish/subscribe declarations. |
| `interprocess` | No | Interprocess-layer publish/subscribe declarations. |
| `intermodule` | No | Intermodule-layer publish/subscribe declarations. |

## `application`

```yaml
application:
  name: <CamelCase application name>      # used as the generated C++ class name
  cpp_type: <C++ application class>       # e.g. goby::zeromq::SingleThreadApplication
  config:                                 # configuration at launch, i.e. the Config template
    scheme: PROTOBUF                      #   parameter of the class named by cpp_type
    type: <protobuf message type>
```

## Transport layers (`interthread`, `interprocess`, `intermodule`)

Each layer accepts either a **single mapping** (one portal) or an **array of mappings** (several
portals, each with its own `alias`). The alias is the name of the C++ accessor for that portal on
the class given by `application: cpp_type` — so if your interprocess portal is reached through
`interblock()`, use `alias: interblock`. For the Goby3 reference implementation these accessor
names are identical to the layer names, so `alias` should be omitted.

### Single portal (mapping form) — typical usage

```yaml
interprocess:
  publishes:
    - group: <C++ group expression>
      scheme: PROTOBUF
      type: <protobuf message type>
  subscribes:
    - group: <C++ group expression>
      scheme: PROTOBUF
      type: <protobuf message type>
```

### Multiple portals (array form) — advanced usage

For a custom application class supporting several portals on one layer:

```yaml
interprocess:
  - alias: interprocess_zeromq  # uses Application::interprocess_zeromq()
    publishes:
      - group: <C++ group expression>
        scheme: PROTOBUF
        type: <protobuf message type>
  - alias: interprocess_udpm    # uses Application::interprocess_udpm()
    subscribes:
      - group: <C++ group expression>
        scheme: PROTOBUF
        type: <protobuf message type>
```

Each entry may independently declare `publishes` and/or `subscribes`.

### Per-publication / per-subscription fields

| Field | Required | Description |
|-------|----------|-------------|
| `group` | **Yes** | C++ group expression, e.g. `project::groups::my_group`. This expression, not the group's runtime name, is how the group is named from Julia or Python — the two need not be the same, and `constexpr Group nav{"navigation"}` is written `project::groups::nav` here. |
| `scheme` | **Yes** | Marshalling scheme. Only `PROTOBUF` is currently supported. |
| `type` | **Yes** | Protobuf message type. May be written with `.` scoping (`project.protobuf.MyMsg`) or C++ `::` scoping (`project::protobuf::MyMsg`); both are normalized to `::` in the generated C++. |

## Complete example

```yaml
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
```

## Language support

The format is defined once, but a given generator may not yet implement every construct the schema
permits. A generator must reject anything it cannot implement with a message that says so, rather
than generating code that silently does the wrong thing.

| Construct | Julia | Python |
|-----------|-------|--------|
| `interthread`, `interprocess`, `intermodule` | yes | yes |
| `scheme: PROTOBUF` | yes | yes |
| multiple portals per layer (`alias`) | yes | yes |

Reserved for future use, and rejected by both generators today: the `intervehicle` layer, and the
`DCCL`, `JSON`, `CSTR` and `MAVLINK` schemes.

## Conformance corpus

`test/valid` and `test/invalid` are exercised by both generators. When adding a feature to the
format, add cases to the corpus in the same commit — the corpus, not the prose, is what keeps the
two generators honest.
