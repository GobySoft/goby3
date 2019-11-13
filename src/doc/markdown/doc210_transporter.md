# Goby Nested Middleware: Transporters

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

## interthread


