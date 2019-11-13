# Goby Nested Middleware: An overview

The Goby `middleware` is provides a starting point for creating complete marine robotic systems. Superficially, it is similar to MOOS and ROS, but with several key distinguishing differences:

* The Goby middleware is designed around a nested communications model that allows for scalable performance across the huge range of throughput values commonly seen in marine systems (tens of bits per second on an acoustic modem to tens of gigabits per second or more between threads).
* Goby does not dictate a particular marshalling scheme unlike ROS (rosmsg) or MOOS (MOOSMsg) does. You can use any serializable type on the interprocess layer that you choose. Currently we like Google Protocol Buffers, but this is not a necessary choice.
* Goby does not dictate a particular interprocess transport mechanism. We like ZeroMQ which is built on TCP/Unix sockets, but the design of Goby3 allows for additional interprocess implementations to be built as needed or desired.


Please read the [User Manual](http://gobysoft.org/dl/goby3-user-manual.pdf) for more information on the design and motivation of the Goby3 middleware. This page is intended to provide technical detail for developers using and modifying it.

## Nested Transport Layers and Transporters

Goby3 is designed around the idea of nested communication layers. Three layers are provided in the current implementation (in `goby/middleware/transport`):

* interthread: Thread to thread comms using C++ std::shared_ptr passing.
* interprocess: Process to process comms using some interprocess transport (e.g. [ZeroMQ](doc500_zeromq.md)).
* intervehicle: Vehicle to vehicle comms using [Goby Acomms](doc100_acomms.md).

A Transporter is used to move data around within a layer or between layers using a publish and subscribe model. In most cases, a Transporter comes in a specific flavor: a Portal (used to actually connect to the transport layer, e.g. modem or socket), and a Forwarder (an similar interface that allows indirect multiple access to a given Portal).

See the [Transporter](doc210_transporter.md) page for more details.
