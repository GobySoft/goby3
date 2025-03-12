# goby-middleware: I/O

The classes in `goby/middleware/io` provide input/output to various non-publish/subscribe interfaces, such as serial ports, UDP sockets, etc.

Each one of these classes is a goby::middleware::SimpleThread that can be easily launched and joined using goby::middleware::MultiThreadApplication.

Each publishes and subscribes to the `goby::middleware::protobuf::IOData` message, which is a thin wrapper around a set of bytes. 
The groups used to publish incoming data and subscribe to outgoing data, as well as the layer on which to do so, are passed as template parameters.

See the [components/middleware/io part of goby3-examples](https://github.com/GobySoft/goby3-examples/tree/master/src/components/middleware/io) for examples on how to use these classes in typical applications.

## Transports

This section details the various transports supported by the Goby3 Middleware I/O classes. For the streaming transports that do not define any framing, these are abstract base classes (and are in the namespace goby::middleware::io::detail) and cannot be used directly. If you are looking for an existing class to use in your Goby Applications, use those that are in goby::middleware::io only, not `detail`.

See the **Framing Protocols** section below for the various protocols supported by specializations of these base classes.

### Serial I/O and PTY I/O

Serial (RS-232, RS-485, RS-422) devices remain common in marine systems. The goby::middleware::io::detail::SerialThread class provides the majority of the functionality for reading and writing to these ports (based on Boost ASIO). The only method that must be implemented is async_read(), as each protocol (ASCII or binary) has its own (often ad-hoc) delimiter or framing rules.

Pseudoterminals (PTYs) are useful for emulating serial ports within a particular machine for simulation purposes. The goby::middleware::io::detail::PTYThread class creates a PTY and connects to the "master" side to feed data to/from the PTY. The "slave" side is then available for another application to connect to (just as if it was connecting to a real serial port).

All the serial/pty implementations in Goby are currently point-to-point. You can use `goby_serial_mux` to create some point-to-multipoint links.

### TCP Server and Client

Transmission Control Protocol (TCP) is a standard stream-based IP transport protocol. The goby::middleware::io::detail::TCPClientThread and goby::middleware::io::detail::TCPServerThread classes provide the interface for creating TCP client and server threads, respectively.

### UDP I/O

User Datagram Protocol (UDP) messages are lightweight IP-based messages. Since the inherent protocol is message-based, no protocol logic is needed here.

#### Point-to-point

The simplest UDP class is goby::middleware::io::UDPPointToPointThread, which has one receiver endpoint. 

#### One-to-many

Slightly more complicated is the goby::middleware::io::UDPOneToManyThread which can address multiple remote endpoints using the  udp_dest field of goby::middleware::protobuf::IOData. 

#### MAVLink binary

goby::middleware::io::UDPThreadMAVLink is nearly the same as goby::middleware::io::SerialThreadMAVLink, but for the UDP transport instead of serial.

### CAN Bus

goby::middleware::io::CanThread provides access to a [CAN bus](https://en.wikipedia.org/wiki/CAN_bus) interface via the [Linux SocketCAN API](https://docs.kernel.org/networking/can.html). As CAN defines a protocol as well as a transport, this thread does not have any protocol specializations.

## Framing Protocols

### Line-based

Line-based interfaces are defined by some end-of-line delimiter (often carriage return and/or newline but can be any character, string or regex). These are typically ASCII based protocols but can, for the purposes of the Goby classes, use be used for any byte stream that is delimited by a single byte, string, or regex.

Examples of protocols that can be read using this format are NMEA-0183, HTTP, and Hayes Command mode.

Implementations:
- Serial: goby::middleware::io::SerialThreadLineBased
- PTY: goby::middleware::io::PTYThreadLineBased
- TCP: goby::middleware::io::TCPClientThreadLineBased and goby::middleware::io::TCPServerThreadLineBased

### COBS

The [Consistent Overhead Byte Stuffing (COBS)](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) algorithm provides unambiguous packet framing regardless of content for a fixed known overhead. It is thus suitable for general use when transmitting arbitrarily sized binary messages.

Implementations:
- Serial: goby::middleware::io::SerialThreadCOBS
- PTY: goby::middleware::io::PTYThreadCOBS
- TCP: goby::middleware::io::TCPClientThreadCOBS and goby::middleware::io::TCPServerThreadCOBS


### MAVLink

[MAVLink](https://mavlink.io/en/) is a lightweight messaging protocol typically used in (aerial) drones.

In addition to the usual publish/subscribe of IOData, these MAVLink classes will publish/subscribe  `mavlink::mavlink_message_t` as well.

Implementations:
- Serial: goby::middleware::io::SerialThreadMAVLink
- UDP (point-to-point): goby::middleware::io::UDPThreadMAVLink