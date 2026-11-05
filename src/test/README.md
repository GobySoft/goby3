# Goby tests

The tests are built with `-Denable_testing=ON` and split by what they exercise:

| | `unit/` | `component/` |
|---|---|---|
| What | One test program (or script) checking a library, tool or binding | A [`goby test`](../doc/markdown/doc010_cli.md) file run against real Goby daemons and applications |
| Runs | Only what the test starts itself | Fixtures it starts (e.g. `gobyd` per vehicle), talking over real transports and links |
| Run with | `ctest -L unit` | `ctest -L component` |

`unit/` keeps the layout of the source it tests (`acomms/`, `middleware/`, `zeromq/`, ...). The tests of `goby test` itself (`unit/middleware/pubsub_test_tool`) are unit tests: they check the tool, not the components it runs.

`component/` is grouped by the behavior under test (e.g. `intervehicle/`). Each directory holds its `.pb.cfg` (or `.yml`) test files and registers each with CTest as `goby test run <file>`.

Some unit tests still `fork()` to stand up several processes (e.g. `unit/zeromq/zeromq_and_intervehicle`); new tests of that kind belong in `component/`.

Test names are `goby_test_*` in both, and generated files follow the source tree (`goby/test/unit/...`, `goby/test/component/...`).
