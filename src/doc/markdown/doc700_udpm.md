# goby-udpm: Using UDP Multicast in Goby

UDP Multicast (UDPM) is a lightweight, broker-free transport layer that uses the standard IP multicast protocol for publish/subscribe messaging. Unlike the ZeroMQ-based transport (which requires a running `gobyd` broker), the UDPM transport is fully peer-to-peer: any process that joins the multicast group can publish and receive messages with no central coordinator.

UDPM is well suited for:
* Local-area or single-machine communication where broadcast or multicast is feasible.
* Environments where running a broker process is undesirable (e.g., embedded systems, small robots).
* Cases where eventual delivery (via NACK-based reliability) is acceptable.

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
| `udp_payload_bytes` | `1472` | Max UDP payload per packet (MTU 1500 − 28 bytes IP/UDP headers) |
| `tx_buffer_size` | `10` | Number of recently sent messages buffered per identifier for NACK retransmission |
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
| 8 | `status` | uint8 | Packet type: `NORMAL=0`, `NACK=1`, `MESSAGE_UNAVAILABLE=2` |

### Data Payload

For `NORMAL` packets, the data payload is the serialized message bytes (the chunk corresponding to the packet's position within the full message). For `NACK` and `MESSAGE_UNAVAILABLE` control packets, there is no data payload.

## Packetization

When a serialized message is larger than `udp_payload_bytes` minus the header overhead (identifier length + 9 bytes), it is automatically split into multiple UDP datagrams. Each datagram carries one "packet" with the same `message_index` and `num_packets`, distinguished by `packet_count = 0, 1, 2, …`.

The receiving side reassembles the message: once all `num_packets` fragments for a given `(identifier, message_index)` pair have arrived, the full serialized message is reconstructed and delivered to subscribers.

### Example: 100 KB Message

With the default `udp_payload_bytes = 1472` and a typical identifier of ~40 bytes:

```
max_data_per_packet = 1472 − (40 + 1 + 9) = 1422 bytes
num_packets = ceil(102400 / 1422) = 72 packets
```

All 72 datagrams carry the same `message_index` and `num_packets = 72`, with `packet_count` ranging from 0 to 71.

## NACK-Based Reliability

UDPM provides optional reliability through a Negative Acknowledgement (NACK) mechanism:

1. **Reception gap detection**: When the receiver has started receiving packets for a multi-packet message but one or more fragments are missing (after detecting a subsequent message arrives or after a poll cycle), it sends a `NACK` packet back to the multicast group. The NACK identifies the specific `(identifier, message_index, packet_count)` that is missing.

2. **TX buffer**: The sender keeps a circular buffer of the `tx_buffer_size` most recently sent messages (by identifier). When a NACK arrives, the sender looks up the requested packet and retransmits it.

3. **MESSAGE_UNAVAILABLE**: If the requested message is no longer in the TX buffer (it has been overwritten by newer messages), the sender responds with a `MESSAGE_UNAVAILABLE` packet. The receiver drops the incomplete reassembly for that message.

The NACK mechanism is not a full reliable-delivery guarantee (the TX buffer is finite and NACKs themselves travel over UDP), but it significantly improves robustness in environments with moderate packet loss.

## No Hold State

Unlike the ZeroMQ transport, UDPM has no "hold" mechanism. The portal immediately begins publishing when started. Subscribers should be set up before publication begins, or they may miss early messages. In the provided `MultiThreadApplication` and `SingleThreadApplication` wrappers, a brief `sleep(1)` is used to allow subscriber processes to start before the publisher begins.

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
4. Send any pending NACK requests for incomplete multi-packet messages.

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
| `_process_received_packet(lock, raw)` | Parses identifier and header; dispatches to reassembly or NACK handling |
| `_check_partial_messages()` | After each poll, sends NACKs for any missing fragments of in-progress reassemblies |
| `_handle_nack(id_key, hdr)` | Looks up the TX buffer for the requested packet and retransmits, or sends MESSAGE_UNAVAILABLE |
| `_poll(lock)` | Drives the Boost.Asio io_context, processes received datagrams, checks partial messages |
