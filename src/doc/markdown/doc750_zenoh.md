# goby-zenoh: Using Zenoh in Goby

[Zenoh](https://zenoh.io/) is a peer-to-peer publish/subscribe protocol with dynamic discovery. Like [UDPM](doc700_udpm.md), and unlike [ZeroMQ](doc500_zeromq.md), the Zenoh transport needs no `gobyd` broker: peers find each other by multicast scouting on the local network, or by explicit endpoints across a routed one.

Zenoh is suited to systems whose processes are spread over more than one machine, where the ZeroMQ transport's single broker becomes a chokepoint or a single point of failure, and where UDPM's lack of reliability is unacceptable.

Zenoh support is optional. It is built when `libzenohc-dev` and `libzenohcpp-dev` are installed (`./DEPENDENCIES -z` adds the Eclipse apt repository and installs them), and can be forced on or off with `cmake -Dbuild_zenoh=ON|OFF`. The Debian packages are `libgoby3-zenoh`, `libgoby3-zenoh-dev` and `goby3-zenoh`.

## Interprocess Portal

`goby::zenoh::InterProcessPortal` implements the [Portal concept](doc210_transporter.md) on a single Zenoh session. `goby::zenoh::InterModulePortal` implements the intermodule layer on the same machinery, separated only by a chunk of the key expression, so one process may run both.

The configuration is given as a `goby::zenoh::protobuf::InterProcessPortalConfig` message.

Key configuration fields:

| Field | Default | Description |
|-------|---------|-------------|
| `platform` | `"default_goby_platform"` | Platform (vehicle/system) name |
| `key_prefix` | `"goby"` | First chunk of every key expression, isolating Goby traffic from anything else sharing the Zenoh network |
| `mode` | `PEER` | `PEER` discovers and connects to other peers directly; `CLIENT` connects only to a router named in `connect_endpoint` |
| `connect_endpoint` | (none) | Endpoint to actively connect to, e.g. `"tcp/192.168.1.10:7447"` |
| `listen_endpoint` | (Zenoh default) | Endpoint to listen on, e.g. `"tcp/0.0.0.0:7447"` |
| `multicast_scouting` | `true` | Discover peers on the local network using multicast scouting |
| `congestion_control` | `BLOCK` | `BLOCK` waits when the network is congested, `DROP` discards the message |
| `express` | `false` | Send immediately rather than batching with other messages to save bandwidth |
| `json5_override` | (none) | Arbitrary Zenoh configuration keys, applied after all of the above |
| `hold` | (none) | Clients to wait for before publishing (see [Hold](#hold)) |
| `client_name` | (app name) | Unique name for this portal instance |

Zenoh's configuration surface is larger and changes faster than this message tracks, so anything not listed can be set through `json5_override`:

```protobuf
interprocess {
    json5_override { key: "transport/link/tx/queue/size/data" value: "8" }
}
```

Zenoh listens on `tcp/[::]:0` by default, which fails outright on a host without IPv6. Set `listen_endpoint: "tcp/0.0.0.0:0"` there.

## Key Expressions

Goby's identifier becomes a Zenoh key expression below a root that names the platform and the layer:

```
<key_prefix>/<platform>/<layer>/<group>/<scheme>/<type>/<process>/<thread>
```

* `key_prefix`, `platform`: from the configuration
* `layer`: `interprocess` or `intermodule`
* `group`, `scheme`, `type`, `process`, `thread`: the same components as the [ZeroMQ identifier](doc500_zeromq.md), which Goby delimits with `/`

The payload is the serialized message, with nothing prepended: the key carries what the other transports carry in a header.

A subscription is made on a key that is wildcarded over the two chunks a publication adds:

```
goby/auv1/interprocess/NavigationReport/PROTOBUF/goby.middleware.frontseat.protobuf.NodeStatus/**
```

and a portal that subscribes to everything (as `goby_zenoh_tool subscribe` does) declares one subscriber on `<root>/**` rather than one per type.

### Escaping

Zenoh rejects `*`, `?`, `#` and `$` in a chunk, and `/` would silently split one chunk into two. Goby group and type names may contain any of them, so each chunk is percent-encoded on the way out and decoded on the way back in; `%` is escaped as well, since it introduces the escape sequence. A group named `a/b` therefore appears in the key as `a%2Fb`.

## Hold

Messages published before their subscriber exists are lost, since Zenoh has no broker to retain them. The `hold` configuration names the clients that must be up first:

```protobuf
interprocess {
    hold {
        required_client: "goby_logger"
        required_client: "goby_liaison"
    }
}
```

While any of them is missing, `hold_state()` is true and publications are buffered rather than sent. Each process announces itself by calling `ready()` once its subscriptions are in place, which declares a Zenoh **liveliness token** at

```
<key_prefix>/<platform>/hold/<layer>/<client_name>
```

Waiting processes subscribe to that space with `history` enabled, so a client that was ready before the waiter started is still seen. Because the token is held by the session, it disappears on its own if the process dies — nothing has to retract it.

Once released the hold is not reapplied: a client that later dies must not silently stop the publications its peers are making.

The hold root sits beside the data root rather than under it, so that a wildcard data subscription does not match the liveliness tokens.

## Applications

`goby::zenoh::SingleThreadApplication` and `goby::zenoh::MultiThreadApplication` provide the same interface as their ZeroMQ counterparts, and `goby::zenoh::SimpleThread` the same as `goby::middleware::SimpleThread`. They are drop-in replacements for use with the Zenoh transport.

The minimal configuration Protobuf message passed to either of these base classes must be:

```protobuf
import "goby/middleware/protobuf/app_config.proto";
import "goby/zenoh/protobuf/interprocess_config.proto";

message BasicApplicationConfig
{
    // required parameters for ApplicationBase3 class
    optional goby.middleware.protobuf.AppConfig app = 1;
    // required parameters for the Zenoh session
    optional goby.zenoh.protobuf.InterProcessPortalConfig interprocess = 2;
}
```

`goby::middleware::Application` reads the `app` field, and `goby::zenoh::InterProcessPortal` reads the `interprocess` field.

## Command Line Tool

`goby zenoh` (`goby_zenoh_tool`) mirrors `goby zeromq` for publishing and subscribing from a shell:

```bash
goby zenoh subscribe NavigationReport
goby zenoh publish NavigationReport goby.middleware.frontseat.protobuf.NodeStatus 'name: "auv1"'
```

## Implementation Details

The portal owns one `zenoh::Session`. Zenoh runs its own threads and delivers samples through subscriber callbacks, so received messages are queued and drained by the main thread on the next `poll()`; the callback wakes a blocked `poll()` through the shared poller condition variable.

### Class Hierarchy

```
InterProcessPortalImplementation<InnerTransporter, PortalBase, ImplementationTag>
  └── PortalBase<Derived, InnerTransporter, ImplementationTag>   (InterProcessPortalBase or InterModulePortalBase)
        └── InterProcessPortalCommon<Derived, InnerTransporter>
              └── StaticTransporterInterface<Derived, InnerTransporter>
```

`InterProcessPortal<InnerTransporter>` and `InterModulePortal<InnerTransporter>` are aliases that differ only in their `PortalBase` and `ImplementationTag`.

### Key Private Methods

| Method | Description |
|--------|-------------|
| `_init()` | Opens the session and, when holding, declares the liveliness subscriber |
| `_do_publish(identifier, bytes)` | Buffers while holding, otherwise puts the sample |
| `_do_portal_subscribe(identifier)` | Declares a subscriber, unless a wildcard subscriber already covers the key |
| `_on_sample(sample)` | Rebuilds the Goby identifier from the key and queues the message (Zenoh thread) |
| `_update_hold(lock)` | Releases the hold and flushes the buffered publications once every required client is ready |
| `_poll(lock)` | Delivers the queued messages to subscribers |
