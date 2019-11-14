# goby-middleware: Transporters

Transporters come in two flavors: either a Portal or a Forwarder:

* Portals connect directly to the "real" transport used for that layer. For example, an interprocess Portal likely connects to a socket or similar (depending on the implementation). Similarly, an intervehicle portal connects to one or more acoustic modems or similar. **Only one portal will exist per conceptual node**, per layer (e.g. each process has exactly one interproess portal, each vehicle has exactly one intervehicle portal).
* Forwarders allow other nodes on a layer to indirectly access the transport resource on that layer by forwarding their data to the relevant portal. **Any number of forwarders may exist per node**. For example, an intervehicle Forwarder can publish data using the inner layer (interprocess) to the intervehicle Portal.

Every Portal or Forwarder will have a inner Transporter (the type is passed as a template parameter, and the instantiation as a constructor argument). If the Transporter is on the innermost layer (interthread for multi-threaded applications, or interprocess for single-threaded applications), this inner Transporter is goby::middleware::NullTransporter, the do-nothing Transporter.


For example, a typical fully realized multi-threaded application might use the following hierarchy:

```
// main thread
InterVehicleForwarder<InterProcessPortal<InterThreadTransporter>>>
// other threads
InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter>>>
```

Generally the `gobyd` is the process running the `InterVehiclePortal`, but this isn't a hard requirement:

```
// gobyd
InterVehiclePortal<InterProcessPortal<InterThreadTransporter>>>
```

## Transporter interfaces

The full interface of a Transporter is composed from several classes (all defined in `goby/middleware/transport/interface.h`) using compile-time polymorphism:

* goby::middleware::StaticTransporterInterface: provides the static (in the sense of using compile-time constexpr goby::middleware::Group) publish/subscribe methods.
* goby::middleware::PollerInterface: provides the poll() methods used to query the underlying "real" transports (e.g. sockets) for data.
* goby::middleware::InnerTransporterInterface: provides access to the inner layer Transporter.

The subclass must implement several publish_dynamic and subscribe_dynamic methods that actually do the publishing and subscribing.

### Publishing data

The typical publication requires four pieces of information (for details, see goby::middleware::StaticTransporterInterface::publish):

   1. The published data *group* (an instantiation of goby::middleware::Group, typically constexpr to allow static analysis of the publish/subscribe graph). The Goby *group* is analogous to MOOS variable *key*, ROS *topics* or LCM *channels*.
   2. The published data *type* (this can generally be inferred from the data itself).
   3. The published data marshalling *scheme* (this is often inferred from the *type*).
   4. The published data itself.

Additionally, a goby::middleware::Publisher can optionally be passed as well to provide more control over the publication or monitor its result. This is typically only used on the outer layers (e.g. intervehicle), and the default (an empty Publisher object) is generally sufficient for interthread and interprocess publications.

Generally the static methods should be preferred (goby::middleware::StaticTransporterInterface::publish) over the dynamic method (e.g. goby::middleware::InterThreadTransporter::publish_dynamic) as this forces use of compile-time (static) goby::middleware::Group instantations. This allows for static generation of the publish/subscribe graph, and hopefully additional static validation in the future. However, the various publish_dynamic calls are valuable when groups are truly runtime defined (such as for arbitrarily scalable applications). The tradeoff here is compile-time checking versus runtime flexibility. C++ in general leans heavily on the former (for good reason when created large real-world systems) so the goal of the Goby middleware is to transfer that design philosophy into the middleware itself.

### Subscribing for data

The subscription requires nearly the same information as publication (see goby::middleware::StaticTransporterInterface::subscribe):

   1. The subscribed data *group*
   2. The subscribed data *type*
   3. The subscribed data *scheme* (this can generally be inferred from the *type*).
   4. A std::function callback to be called when the subscribed data arrives. This callback will only be called from goby::middleware::PollerInterface::poll. Note that C++ lamdbas are implicitly convertible to std::function, so those are often a good choice for this parameter.

Additionally, a goby::middleware::Subscriber can optionally be passed as well to provide more control over the subscription. Like the goby::middleware::Publisher, this is typically only used on the outer layers (e.g. intervehicle).

## interthread

The **interthread** layer is typically the innermost layer for multithreaded Goby3 applications, so there is no distinction between Portals and Forwarders. The implementation provided by Goby is the goby::middleware::InterThreadTransporter, which uses std::shared_ptr and STL containers to provide the publish/subscribe transport.

If the std::shared_ptr variants of goby::middleware::StaticTransporterInterface::publish are used, no copy of the data is made, making the InterThreadTransporter quite efficient. In this case, the publisher must not modify the data after publication, or else thread-safety is violated.

```
namespace groups
{
constexpr goby::middleware::Group nav;
}

InterThreadTransporter interthread;
auto data = std::make_shared<std::string>("my navigation data");
interthread.publish<groups::nav>(data);
// after this point 'data' should not be mutated (but may be read or re-published).
```

Since shared pointers are used (and no serialization/parsing is done), any data type (primitive types, STL containers, user classes, user structs, etc.) can be used for this layer.

## interprocess

The **interprocess** layer provides process to process communications, typically for processes on the same host or connected via a fast, reliable link (e.g. several computers in the same payload housing of a vehicle connected via wired Ethernet).

Goby3 provides a Forwarder implementation (goby::middleware::InterProcessForwarder), and a reference implementation of the Portal using the ZeroMQ message passing library (for details see the documentation on the [Goby ZeroMQ library](doc500_zeromq.md).)

Any data type that can be serialized to a message of bytes is suitable for use on the interprocess layer. For more details, see the [marshalling](doc220_marshalling.md) documentation page.

## intervehicle

The **intervehicle** layer is the most complicated because it is designed to allow support publish/subscribe messaging over an unreliable slow link (such as an acoustic modem) by taking advantage of the goby-acomms functionality.

The Forwarder implementation is goby::middleware::InterVehicleForwarder, which uses its inner transporter (typically one of the interprocess Transporters) to forward data to and from the Portal.

The Portal implementation is goby::middleware::InterVehiclePortal, which allows one or more physical (vehicle-to-vehicle) links to be specified for use. The `gobyd` process runs an InterVehiclePortal, so this is a useful place to look for use on this class.

The InterVehiclePortal is configured using a goby::middleware::intervehicle::protobuf::PortalConfig Protobuf message. The contents of this message is described in the [User Manual](http://gobysoft.org/dl/goby3-user-manual.pdf) under the `gobyd` section.

Each link on the InterVehiclePortal has an 15-bit modem id (roughly similar to an IP address) and a 15-bit subnet mask. The logical AND of the modem id and the subnet mask provides the given subnet. Each link should be on its own logical subnet. The 0 address on a given subnet is used as broadcast.

For example, using a subnet mask of 0xFFF0 provides subnets with 16 addresses each:

* 0 (broadcast), 1-15 (subnet 1 for link 1, e.g. WHOI Micro-Modem)
* 16 (broadcast), 17-31 (subnet 2 for link 2, e.g. Iridium satellite)
* ...

### Publisher

An instantiation of the goby::middleware::Publisher class can be passed to calls to InterVehicleForwarder and InterVehiclePortal publish for more information and control.

The Publisher can contain the following:

* A goby::middleware::protobuf::TransporterConfig that defines the DynamicBuffer parameters for this publication.
* A callback for setting the *group* in the message itself. To save space on intervehicle publications, only the DCCL message is actually sent on the wire (no additional metadata is added). In order to allow multiple groups (if required), the group must be set somewhere in the published DCCL message itself. This is up to the designer of the DCCL message to determine, and this callback function is used to actually set this field in the DCCL message. A reciprocal function is provided in goby::middleware::Subscriber to extract the *group*.
* A callback for receiving acknowledgments. Since **intervehicle** links are typically unreliable, it can sometimes be helpful to know if a publication was actually received by one or more subscribers. If this callback is provided, `ack_required` is automatically set true in the DynamicBuffer configuration.
* A callback for receiving expired messages. If a message exceeds its time-to-live without being acknowledged, this function is called, so the publisher can take further action (re-publish an updated message, post a warning, etc.)

### Subscriber

On the **intervehicle** layer, subscriptions are published DCCL messages themselves (goby::middleware::intervehicle::protobuf::Subscription). A goby::middleware::InterVehiclePortal does not actually send any data "on the wire" unless a subscriber exists for a given publication.

This requires that the subscriber explicitly define the modem id(s) for one or more prospective publishers in the `publisher_id` field of the goby::middleware::intervehicle::protobuf::TransporterConfig message. Each of these publishers will be sent a Subscription message, and if they are publishing (or begin publishing) the matching information, then these data will actually be sent "on the wire" (e.g. acoustically).

Given this, the configuration for goby::middleware::Subscriber looks similar to that of the goby::middleware::Publisher:

* A goby::middleware::protobuf::TransporterConfig message that contains the desired publisher_id(s) and also possibly the DynamicBuffer configuration. If both the Publisher and Subscriber define buffer configuration, they are merged as described in the documentation for goby::acomms::DynamicBuffer.
* A callback for extracting the *group* from the subscribed DCCL message. As explained in the Publisher section above, the *group* must be embedded in the DCCL message (if it is not goby::middleware::Group::broadcast_group). This callback is used to provide the Goby middleware with the group as defined for this particular DCCL message.
* Since the Subscription is just another DCCL message that could be lost on an unreliable link, a callback can be registered for successful subscription (acknowledgement of the Subscription message by a publisher node). When this function is called, the subscribing node knows the subscription has been set up successfully.
* A callback for expired subscriptions (if the Subscription message reaches its TTL without being acknowledged by one or publisher nodes).

## Group

The goby::middleware::Group is a class that allows distinguishing different conceptual groupings of a particular data type. The *scheme*, *type*, and *group* fully define any publication or subscription.

A Group can have either a string or 8-bit numeric (uint8_t) representation, or both.

For interthread and interprocess use, the Group is defined by its string representation. For the intervehicle layer, the Group's numeric representation is used (which can be transmitted much more  efficiently). Since all publications also publish to their inner layers, a Group used on intervehicle layer will need both a string and a numeric representation.

For example:

```
constexpr goby::middleware::Group{"groupA"}; // useable for interthread/interprocess
constexpr goby::middleware::Group{"groupB", 2}; // usable for all layers
```

Group numbers 0 and 255 are reserved for `broadcast` and `invalid group`, respectively. The `broadcast` group is essentially a wildcard, and is useful when the *scheme*/*type* pair alone is sufficient to distinguish publications.