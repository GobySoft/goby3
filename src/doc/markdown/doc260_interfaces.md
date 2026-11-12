# goby-middleware: Interfaces and deployments

Goby's groups, types and schemes are known at compile time, so the publish/subscribe interface of every application can be read from its source. `goby_clang_tool -gen` writes it to an interface file, and a deployment file says which applications run where. From these, `goby interface` checks and draws a whole system of vehicles and applications without running it.

<!-- Outline: sections below are to be filled out. -->

## Interface files

- Written by `goby_clang_tool -gen -target <target>`, usually through `goby_export_interface()` in `cmake_modules/GobyClangTool.cmake`; Goby's own are installed in `share/goby/interfaces`.
- One file per application: `application:`, then `interthread` (per thread, with `bases`), `interprocess`, `intermodule` and `intervehicle`, each with `publishes` and `subscribes`.
- Each entry: `group`, `scheme`, `type`, `thread`; subscriptions add `necessity`; `inner: true` marks a publication repeated on an inner layer; `is_regex: true` marks a type-regex subscription.
- Applications written in Python or Julia: run `-gen` on the generated glue target to get the same file.

## Necessity

- `required`, `recommended`, `optional` (default): the template parameter of `subscribe()`, `subscribe_dynamic()` and `subscribe_type_regex()`, or `necessity:` in a Python or Julia `interface.yml`.
- Static metadata only: nothing reads it at run time.
- Drawn bold (required), tapered (recommended) or solid; a required subscription without a publisher is drawn red.

## Dynamic groups

- What `-gen` can tell about a `DynamicGroup`: the whole group (`dynamic: true`), its string but not its numeric (`numeric: runtime`), or neither (`group: "*"`, with `expr`).
- How it follows a group back to where it is made: a literal, a member's initializer, a local or global variable, a helper function's return value, a smart pointer's `reset(new DynamicGroup(...))`.

## Deployment files

- `deployment:` name; `platforms:` each with `name:` and either `run:` or `modules:` (each with `name:` and `run:`).
- `run:` entries: an application name, or `{ application, name, plugins, file, bind }`.
- `instances:`, `bind:` and the top-level `necessity:` overrides.
- Where interface files are found; `${NAME}` variables.
- The older `interfaces:` list of files.

## Scopes

<!-- Figure: one platform with two modules and a second platform, showing which edges each layer can draw. -->

- interthread: within an application
- interprocess: within a module (one `gobyd`)
- intermodule: across the modules of a platform
- intervehicle: between platforms

## Checking and drawing

- `goby interface validate`: issue codes and exit status (see [goby interface](doc010_cli.md)).
- What a diagram leaves out by default (gobyd on its container, other Goby applications as one box, Goby's internal groups and threads) and the options that draw them.
- `goby interface explain` for a single group; `list` and `diff` for inventories and regressions; `init` to start a deployment file.

## Launching a deployment

- The launch keys on a `run` entry (`process`, `exec`, `config`, `args`, `env`, `kill`, `when`, `enabled`) and a platform's `launch: { flags, defaults }`.
- Derived lists: `hold: auto` for `gobyd`, `expected: auto` for `goby_coroner`.
- `goby interface launch`, `goby launch <deployment>.yml`, and `launch_check` for moving existing launch files over.
- `goby interface systemd`: `systemd:` settings, templates, `when` decided at write time, and `--check` against installed units.
- `goby interface visualize` and `goby_clang_tool -viz`.
- `goby_validate_interfaces()` in CMake.
