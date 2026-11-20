# `interface.yml` — Goby language binding interface definition

This directory holds the normative definition of the `interface.yml` file used by all the Goby
language bindings. The format is shared exactly: the same file drives the Julia generator
(`gen_goby.jl`, in `src/share/Goby.jl`) and the Python generator (`goby.gen`, in `src/python`).

| File | Purpose |
|------|---------|
| `README.md` | This document — the normative description of the format. |
| `interface.schema.json` | Machine-readable [JSON Schema](https://json-schema.org) for the same format (YAML is JSON-compatible, so the schema applies directly). |
| `test/valid/*.yml` | Conformance corpus: every generator must accept these. |
| `test/invalid/*.yml` | Conformance corpus: every generator *and* the schema must reject these. |
| `test/invalid-semantic/*.yml` | Conformance corpus: every generator must reject these, but the schema accepts them — what is wrong is a relationship between entries, which JSON Schema cannot express. |

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
| `interthread` | No | Interthread-layer publish/subscribe declarations: the contract the binding enforces between its threads (see below). |
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
| `necessity` | No | Subscriptions only: `required`, `recommended` or `optional` (the default). Passed to the generated `subscribe()`, so `goby_clang_tool -gen` records it and `goby interface validate` can check that a deployment provides a publisher. |

### `interthread` entries

The bindings carry interthread in their own language, so its entries describe threads of that
language rather than C++ calls, and take three more fields:

| Field | Required | Description |
|-------|----------|-------------|
| `group` | **Yes** | The string the binding delivers on. No C++ group needs to exist. |
| `scheme` | **Yes** | `PROTOBUF`, or `NATIVE` for any other type of the bound language. Either way the object is handed over as it is. `NATIVE` is interthread only. |
| `type` | **Yes** | A protobuf message type, or a type of the bound language as that language names it. Resolved when the application starts. |
| `thread` | No | A Julia task module's name (`Main` for the main task), or a Python `Thread` subclass's or application class's name. Without it the entry applies to any thread. |
| `capacity` | No | Subscriptions only, Julia only: messages queued for the subscription, default 50. |
| `overflow` | No | Subscriptions only, Julia only: what a publication does when the queue is full: `block` (default), `drop_oldest`, `drop_newest` or `error`. |

Once a file declares `interthread`, the binding holds every interthread publication and
subscription to it, and an undeclared one is an error. Without the section, interthread groups are
any string and messages any value.

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
| `interthread` declarations are enforced once present | yes | yes |
| `scheme: PROTOBUF` | yes | yes |
| `scheme: NATIVE` (interthread) | yes | yes |
| interthread `thread` | yes | yes |
| interthread `capacity` and `overflow` | yes | no |
| multiple portals per layer (`alias`) | yes | yes |
| generated module of group constants and accessors | yes | yes |

Reserved for future use, and rejected by both generators today: the `intervehicle` layer, and the
`DCCL`, `JSON`, `CSTR` and `MAVLINK` schemes.

Both bindings implement the `interthread` layer in their own language rather than through the
C++ `InterThreadTransporter`, so neither generates C++ for it. Declaring it is optional; when it is
declared, the groups become named constants in the generated module and the declaration is the
contract the binding enforces.

An `alias` names the accessor the application calls, but only the layer crosses into C++, so it
cannot distinguish two portals on the same layer that declare the same scheme, type and group.
Rather than silently resolve that to whichever came first, both generators reject it — declare the
portals with distinct groups or types.

## What a generator produces

Each generator writes two files from the interface file: the C++ glue that makes the statically
typed Goby calls, and a module in its own language holding the declared groups and one accessor
per portal. The group constant is the `group` expression, which is the name the C++ glue matches
on, so an application names its groups rather than repeating the expression as a string.

| | Julia | Python |
|--|-------|--------|
| C++ glue | `<target>.cpp` | `<target>.cpp` |
| language module | `<target>_goby.jl`, module `<name>Goby` | `<name_snake>_goby.py` |
| interthread manifest | `<target>_julia_interthread.yml` | `<target>_python_interthread.yml` |

The manifest is the declared `interthread` section as an interface file would hold it. The
binding's CMake function passes it to `goby_clang_tool -gen -interthread-manifest`, which adds it
to `<target>_interface.yml` beside what it read from the glue, so that `goby interface` sees the
application's threads.

## Conformance corpus

`test/valid`, `test/invalid` and `test/invalid-semantic` are exercised by both generators. When
adding a feature to the format, add cases to the corpus in the same commit — the corpus, not the
prose, is what keeps the two generators honest. A case belongs in `invalid-semantic` rather than
`invalid` when the schema cannot express the rule, which is anything relating two entries to each
other; the schema is checked against both, so putting one in the wrong place fails.

Run both over the corpus with the `goby_test_interface_conformance` test:

    cmake -Denable_testing=ON -Dbuild_julia=ON -Denable_interface_conformance_test=ON ..

It is off by default because it starts Julia once per case, which takes a couple of minutes; CI
runs it on every commit. `goby_test_julia_app` and `goby_test_python_app` then compile what each
generator writes, and both run by default.
