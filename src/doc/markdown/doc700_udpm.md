# goby-udpm: Using UDP Multicast in Goby

UDP Multicast (UDPM) is a lightweight, broker-free transport layer that uses the standard IP multicast protocol for publish/subscribe messaging. Unlike the ZeroMQ-based transport (which requires a running `gobyd` broker), the UDPM transport is fully peer-to-peer: any process that joins the multicast group can directly publish and receive messages.

UDPM is suited for high-throughput applications with multiple subscribers. As UDP has no reliability (ack) implementation, **applications must be comfortable with potential data loss** and/or be willing to handle reliability at the application level.



## Interprocess Portal

The `goby::udpm::InterProcessPortal` implements the [Portal concept](doc210_transporter.md) using a UDP multicast socket. Each process that uses the UDPM portal opens a single UDP socket that is both used for sending (to the multicast group) and receiving (by joining the multicast group). There is no separate router or manager process.

The configuration is given as a `goby::udpm::protobuf::InterProcessPortalConfig` message.

Key configuration fields:

| Field | Default | Description |
|-------|---------|-------------|
| `platform` | `"default_goby_platform"` | Platform (vehicle/system) name |
| `listen_address` | `"0.0.0.0"` | Local address to bind the UDP socket |
| `multicast_address` | `"239.142.0.2"` | IPv4 multicast group address |
| `multicast_port` | `11144` | UDP port for all multicast traffic |
| `udp_payload_bytes` | `1472` | Max UDP payload per packet (MTU 1500 − 28 bytes IP/UDP headers). Messages greater than this packet size will be automatically packetized as needed. |
| `max_send_rate_bytes_per_second` | `0` (unlimited) | Maximum transmit rate in bytes per second. Set to `0` to disable rate limiting. For example, set to `12500000` to limit to 100 Mbps. |
| `client_name` | (app name) | Unique name for this portal instance |

## Wire Protocol

Each UDP datagram carries a single UDPM packet. The packet layout is:

```
[null-terminated identifier][9-byte fixed header][data payload]
```

### Identifier

The identifier is the same null-terminated `/`-delimited string used by the ZeroMQ transport:

```
/group/scheme/type/process/thread/\0
```

These parts are as follows:

* `group`: String representation of the `goby::middleware::Group`
* `scheme`: String representation of the marshalling scheme
* `type`: Type name returned by `goby::middleware::SerializerParserHelper::type_name()`
* `process`: String representation of the publishing process id (`std::to_string(getpid())`)
* `thread`: Hex representation of the std::hash of the publishing thread id

### Fixed Header (9 bytes)

After the null terminator, every packet carries a 9-byte header encoded in network byte order:

| Bytes | Field | Type | Description |
|-------|-------|------|-------------|
| 0–3 | `message_index` | uint32 | Monotonically increasing index per identifier, wraps at 2^32 |
| 4–5 | `num_packets` | uint16 | Total number of packets in this message (1 for messages that fit in a single datagram) |
| 6–7 | `packet_count` | uint16 | Zero-based index of this packet within the message |
| 8 | `status` | uint8 | Packet type: `NORMAL=0` (this isa placeholder for future protocol enhancements) |

### Data Payload

For `NORMAL` packets, the data payload is the serialized message bytes (the chunk corresponding to the packet's position within the full message). 

## Packetization

When a serialized message (including header) is larger than `udp_payload_bytes`, it is automatically split into multiple UDP datagrams. Each datagram carries one "packet" with the same `message_index` and `num_packets`, distinguished by `packet_count = 0, 1, 2, …`.

The receiving side reassembles the message: once all `num_packets` fragments for a given `(identifier, message_index)` pair have arrived, the full serialized message is reconstructed and delivered to subscribers.

### Example: 100 KB Message

With the default `udp_payload_bytes = 1472` and a typical identifier of ~40 bytes:

```
max_data_per_packet = 1472 − (40 + 1 + 9) = 1422 bytes
num_packets = ceil(102400 / 1422) = 72 packets
```

All 72 datagrams carry the same `message_index` and `num_packets = 72`, with `packet_count` ranging from 0 to 71.

## Rate Limiting

The `max_send_rate_bytes_per_second` configuration field allows limiting the transmit rate of the UDPM portal. When set to a non-zero value, packets are queued internally and sent no faster than the configured rate.

Rate limiting uses an inter-packet gap approach: after each UDP datagram is sent, the next datagram in the queue is held until enough time has elapsed such that the average transmit rate does not exceed the configured limit.

The rate limit applies to the raw UDP payload bytes (including the UDPM header and identifier). It does not account for IP/UDP framing overhead at the network layer.

### Example: Limiting to 100 Mbps

```
max_send_rate_bytes_per_second: 12500000  # 100 Mbps = 100,000,000 bits/sec / 8 = 12,500,000 bytes/sec
```

When no rate limit is set (the default, `max_send_rate_bytes_per_second = 0`), packets are sent as fast as the UDP socket allows with no queuing overhead.

## No Hold State

Unlike the ZeroMQ transport, UDPM has no "hold" mechanism. The portal immediately begins publishing when started. Subscribers should be set up before publication begins, or they may miss early messages.

## Applications

The `goby::udpm::SingleThreadApplication` and `goby::udpm::MultiThreadApplication` provide the same interface as their ZeroMQ counterparts. They are drop-in replacements for use with the UDPM transport.

The minimal configuration Protobuf message passed to either of these base classes must be:

```protobuf
import "goby/middleware/protobuf/app_config.proto";
import "goby/udpm/protobuf/interprocess_config.proto";

message BasicApplicationConfig
{
    // required parameters for ApplicationBase3 class
    optional goby.middleware.protobuf.AppConfig app = 1;
    // required parameters for connecting to the UDPM multicast group
    optional goby.udpm.protobuf.InterProcessPortalConfig interprocess = 2;
}
```

`goby::middleware::Application` reads the `app` field, and `goby::udpm::InterProcessPortal` reads the `interprocess` field.

## Implementation Details

The `goby::udpm::InterProcessPortal` is a single-threaded portal: all sending and receiving are performed on the calling thread via a Boost.Asio `io_context`. During each call to `poll()`, the io_context is polled to:

1. Process any asynchronously received UDP datagrams.
2. Decode the identifier and packet header.
3. Reassemble multi-packet messages.

Asynchronous sends are used so that the `publish()` path does not block waiting for the socket write to complete. A shared pointer to the packet buffer is captured in the send completion handler, ensuring the data lives until the OS has consumed it.

### Class Hierarchy

```
InterProcessPortalImplementation<InnerTransporter, PortalBase>
  └── PortalBase<Derived, InnerTransporter>   (InterProcessPortalBase or InterProcessPortalCommon)
        └── InterProcessPortalCommon<Derived, InnerTransporter>
              └── StaticTransporterInterface<Derived, InnerTransporter>
```

The `InterProcessPortal<InnerTransporter>` alias uses `InterProcessPortalBase` as the `PortalBase`.

### Key Private Methods

| Method | Description |
|--------|-------------|
| `_init()` | Opens the UDP socket, joins the multicast group, starts the first async receive |
| `_do_publish(identifier, bytes)` | Packetizes and asynchronously sends all packets for a message |
| `_process_received_packet(lock, raw)` | Parses identifier and header; dispatches to reassembly |
| `_poll(lock)` | Drives the Boost.Asio io_context, processes received datagrams, checks partial messages |
