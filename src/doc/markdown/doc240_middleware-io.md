# goby-middleware: I/O

The classes in `goby/middleware/io` provide input/output to various non-publish/subscribe interfaces, such as serial ports, UDP sockets, etc.

Each one of these classes is a goby::middleware::SimpleThread that can be easily launched and joined using goby::middleware::MultiThreadApplication.

Each publishes and subscribes the goby::middleware::protobuf::IOData message, which is a thin wrapper around a set of bytes.

The groups used to publish incoming data and subscribe to outgoing data, as well as the layer on which to do so, are passed as template parameters.

## Serial I/O

Serial (RS-232, RS-485, RS-422) devices are still very common on marine systems. The goby::middleware::io::SerialThread provides the majority of the functionality for reading and writing to these ports (based on Boost ASIO). The only method that must be implemented is async_read(), as each protocol (ASCII or binary) has its own (often ad-hoc) delimiter or framing rules.

All the serial implementations in Goby are currently point-to-point.

### Line-based ASCII

goby::middleware::io::SerialThreadLineBased implements SerialThread for line-based serial interfaces (those that are delimited by a byte or set of bytes). These are typically ASCII based protocols, often delimited by end-of-line and/or carriage-return characters.

goby::middleware::io::SerialThreadLineBased can use a regex as the delimiter, allowing for multiple delimiters (as some sensors have been known to use).

### MAVLink binary

goby::middleware::io::SerialThreadMAVLink implements SerialThread for the MAVLink binary protocol (over serial).

In addition to the usual publish/subscribe of IOData, this MAVLink class will publish/subscribe  `mavlink::mavlink_message_t` as well.

## UDP I/O

User Datagram Protocol (UDP) messages are simple IP messages. Since the inherent protocol is message-based, no delimiter logic is needed here.

### Point-to-point

The simplest UDP class is goby::middleware::io::UDPPointToPointThread, which has one receiver endpoint. 

### One-to-many

Slightly more complicated is the goby::middleware::io::UDPOneToManyThread which can address multiple remote endpoints using the  udp_dest field of goby::middleware::protobuf::IOData. 

### MAVLink binary

goby::middleware::io::UDPThreadMAVLink is nearly the same as goby::middleware::io::SerialThreadMAVLink, but for the UDP transport instead of serial.