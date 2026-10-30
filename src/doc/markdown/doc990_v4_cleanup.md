# Cleanups deferred to Goby 4

A running list of changes that are worth making but that break released API, ABI, or the
configuration command line, and so have to wait for the next major version. Add to it whenever a
cleanup is identified and deferred, rather than losing it in a commit message.

Each entry records what to change and why it can't be done in a 3.x release.

## Transport implementations

### Retire the one-argument transporter specializations

`InterProcessForwarder<Inner, void>`, `InterModuleForwarder<Inner, void>` and
`SimpleThread<Config, void>` exist only so that code written before the `ImplementationTag`
parameter keeps compiling; they resolve to the ZeroMQ implementation and their constructors are
marked `[[deprecated]]`. To declare them, three core headers include
`zeromq/transport/detail/tags.h`, so `libgoby`'s public headers depend on a file in the optional
`src/zeromq` tree (`middleware/transport/interprocess.h`, `middleware/transport/intermodule.h`,
`middleware/application/simple_thread.h`).

Forward declaring the tag instead of including it does not work while the specializations remain:
the tag then has to be complete wherever one of them is used, so a translation unit that uses
`middleware::SimpleThread<Config>` without also including a ZeroMQ header stops compiling — which
is the breakage the specializations exist to prevent.

Remove the specializations, and the include goes with them. `ImplementationTag` then needs no
default and no implementation is named anywhere in core middleware.

### Take an ImplementationTag, not a portal template

`SimpleThread` selects its implementation with a tag; `SingleThreadApplication` and
`MultiThreadApplication` take a portal template instead. The tag form is available as
`SingleThreadApplicationFor` / `MultiThreadApplicationFor`, but having both is the real problem.
Change the second parameter of `SingleThreadApplication` and `MultiThreadApplication` to a tag,
drop the `...For` aliases, and let the per-implementation aliases
(`zeromq::SingleThreadApplication`, etc.) be written the same way at every layer.

### Move the ModemDriverThread instantiations out of core

`middleware/transport/intervehicle/driver_thread.cpp` explicitly instantiates
`ModemDriverThread` for each implementation tag, so it includes each implementation's `tags.h` and
`libgoby` exports symbols for implementations that live in other libraries. Each implementation
should instantiate its own, in its own library — moving an exported symbol out of `libgoby` is the
ABI change that holds this back.

### Declare implementation tags with less boilerplate

`zeromq/transport/detail/tags.h` and `udpm/transport/detail/tags.h` are 50 lines each to declare
two structs holding one string. A `GOBY_DECLARE_TRANSPORT_TAGS(ns, "prefix")` macro would make each
one line. Safe in itself, but only worth doing alongside the tag changes above.

### Hand the receive path parsed fields

`InterProcessPortalCommon::_handle_received_data()` takes the identifier, a null delimiter and the
payload as one flat `std::string`, then re-parses the identifier the caller already had. UDPM
reassembles that string from fragments to satisfy it. An overload taking the parsed fields and a
payload range removes a parse and a copy per message, and lets an implementation that carries
metadata out of band (for instance in a key expression or a message attachment) avoid synthesizing
the string at all.

### Make scheme lookup independent of include order

`InterProcessTransporterBase::scheme()` calls `goby::middleware::scheme<Data>()` with no arguments,
so the overload set is fixed at template definition and ADL cannot extend it at instantiation. Every
marshalling scheme must therefore be declared before the transport headers are first parsed, and
reordering includes in an unrelated header can break an unrelated translation unit. Taking a
`Data*` or a tag argument, or moving the mapping to a trait specialized per scheme, would make the
declaration order irrelevant.

## Configuration

### One tool configuration, not one per implementation

`goby.middleware.protobuf.PublishToolConfig`/`SubscribeToolConfig` and their
`goby.apps.zeromq.protobuf` counterparts are identical except for the type of the `interprocess`
field, and the core pair is typed on the UDPM portal config — so `middleware/protobuf/tool_config.proto`
imports `udpm/protobuf/interprocess_config.proto`, pointing core at an implementation.

Sharing one definition needs one of:

  * proto2 extensions for the portal config, which requires `ConfigReader` to walk extension ranges
    (it iterates `Descriptor::field_count()` today, so extension fields would not appear in `--help`
    or be settable from the command line); or
  * generating the per-implementation configs from one template at build time, which puts `.proto`
    files that are read as documentation into the build directory.

Either way the message names or their fields move, which changes released protobuf types.

### Make the tool configuration packages consistent

The ZeroMQ tool config is `goby.apps.zeromq.protobuf`, the UDPM one `goby.udpm.protobuf`. Pick one
convention when the messages move.

## Tests and documentation

### Select the implementation under test without an `#if` ladder

The CMake side is parameterized — `create_single_thread_app1_test(udpm "")` — but each test body
carries an `#if defined(test_for_zeromq) / #elif defined(test_for_udpm)` chain that grows a branch
per implementation. A single header resolving the define to a namespace alias and a config typedef
would keep the bodies implementation-agnostic.

### Describe the transport concepts once

`doc500_zeromq.md` and `doc700_udpm.md` each re-explain the portal concept, the identifier format
and the configuration conventions. Those belong in `doc210_transporter.md`, with a comparison table,
leaving each implementation chapter to cover only what is specific to it.
