# goby-zeromq: Using ZeroMQ in Goby

ZeroMQ is a lightweight transport layer that extends the UNIX socket concept to more abstract models, one of which is publish/subscribe.

Given its ease of use and portability, it was chosen for the first (reference) implementation of the Goby interprocess Portal.

## Interprocess Portal

The goby::zeromq::InterProcessPortal implements the [Portal concept](doc210_transporter.md) using a broker (typically `gobyd`) containing a zmq::proxy connecting a XPUB frontend and an XSUB backend. The actual zmq::proxy resides in the goby::zeromq::Router class which is run in its own thread. This use of XSUB/XPUB allows multiple publishers of the same data type.

To avoid having to configure two sockets for each client (XPUB and XSUB), these are dynamically allocated by the goby::zeromq::Manager.

The configuration for the Manager is given as a goby::zeromq::protobuf::InterProcessPortalConfig message.

The Manager opens a ZMQ_REP socket on a known address based on the the configuration's `transport` enumeration:

* goby::zeromq::protobuf::InterProcessPortalConfig::IPC, then "ipc://socket_name" where socket_name is "/tmp/goby_{platform}.manager" unless explicity specified in `socket_name`.
* goby::zeromq::protobuf::InterProcessPortalConfig::TCP, then "tcp://*:port" where `port` is also given in the configuration's `tcp_port`.

When a new client (goby::zeromq::InterProcessPortal) connects to the goby::zeromq::Manager, it sends a request using goby::zeromq::protobuf::ManagerRequest, with the request enumeration of goby::zeromq::protobuf::PROVIDE_PUB_SUB_SOCKETS.

In response, the Manager provides a goby::zeromq::protobuf::ManagerRequest containing the actual sockets required for data (the Router's XPUB/XSUB sockets) in the `subscribe_socket` and `publish_socket` fields.

Once these are received, the application can receive data by subscribing using ZMQ_SUB to the provided `subscribe_socket` and publishing using ZMQ_PUB to the `publish_socket`.

## Wire protocol

The protocol for Goby messages on ZeroMQ consistes of an identifier followed by the encoded data.

The identifier is a "/" delimited string (analogous to a file path) using the following structure:

```
/group/scheme/type/process/thread
```

These parts are as follows:

* group: The string representation of the goby::middleware::Group
* scheme: The string representation of the scheme if one is defined in goby::middleware::MarshallingScheme::e2s, otherwsie the numeric value as a string (std::to_string)
* type: The type name as returned by goby::middleware::SerializerParserHelper::type_name() for the given message
* process: string representation of the process id (std::to_string(getpid())).
* 

Since ZMQ allows wildcard subscription based on substrings, you can subscribe at any point for more messages (don't forget the ending slash or you may get unintended messages if you wanted "Foo" but not "FooBar"):

* "/" subscribes to all messages
* "/group/scheme/" subscribes to all messages of a given group and scheme (from all processes and all threads)
* "/group/scheme/type/" subscribes to all messages of a given group, scheme, and type, but from any process or thread (this is what is typically used by InterProcessPortal).
* "/group/scheme/type/process/" subscribes to a fully qualified message from a particular process name

