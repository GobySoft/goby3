# goby-middleware: Applications

Goby provides some classes that make it easy to quickly build complete applications (binaries) without having to write them from scratch.

These application-related classes are also a good reference for how to use the Transporters if you wish to build your own applications from scratch.

All the Goby applications are run using goby::run().

## Base class: Application

goby::middleware::Application is the base class for all Goby applications. It performs only a few simple tasks, all of which are complete when its constructor exits:

* Reads configuration from the command line using a Configurator
* Sets up the goby::glog debug logger
* Sets up the goby::time::SimulatorSettings simulation parameters (if in use)
* Sets up a goby::util::UTMGeodesy if desired.

Additionally, it provides several virtual methods:

* goby::middleware::Application::initialize(): Called after construction but before run(). Optional, the default does nothing.
* goby::middleware::Application::finalize(): Called just before destruction after run() completes. Optional, the default does nothing.
* goby::middleware::Application::run(): Must be implemented - it is called repeatedly until goby::middleware::Application::quit() is called.

## Configurators

The goby::middleware::ConfiguratorInterface can be subclassed to automatically turn command-line configuration (and may also open configuration files) into an object representing this configuration for the application to use.

Currently the implemented configurator is the goby::middleware::ProtobufConfigurator which uses a Protocol Buffers message to represent the allowable configuration for an application. From this message, allowable command-line and configuration file options are produced. These are then validated against the Protobuf Descriptor.

## SingleThreadApplication

*Note:* most users will not instantiate a goby::middleware::SingleThreadApplication directly, but rather use a particular interprocess implementation such as goby::zeromq::SingleThreadApplication.

The goby::middleware::SingleThreadApplication is designed for users to quickly access an InterProcessPortal (via goby::middleware::SingleThreadApplication::interprocess()) and an InterVehicleForwarder (via goby::middleware::SingleThreadApplication::intervehicle()). As the name suggests, this application has no interthread layer (and thus no InterThreadTransporter).

The main (only) thread inherits from goby::middleware::Thread, which provides an optional method goby::middleware::Thread::loop() that is called regularly at the frequency passed to the constructor.

The loop() method will never be called when any of the callbacks passed to goby::middleware::StaticTransporterInterface::subscribe are called, so if the subscription callbacks block it may delay the loop() call.

## MultiThreadApplication

*Note:* most users will not instantiate a goby::middleware::MultiThreadApplication directly, but rather use a particular interprocess implementation such as goby::zeromq::MultiThreadApplication.

The goby::middleware::MultiThreadApplication has the same interface as the SingleThreadApplication but with the addition of an InterThreadTransporter (accessed using goby::middleware::SingleThreadApplication::interthread()) as the innermost layer. This allows thread-to-thread comms.

User applications can inherit from goby::middleware::SimpleThread to create additional threads, and then manage them from the main thread with goby::middleware::MultiThreadApplication::launch_thread() and goby::middleware::MultiThreadApplication::join_thread().

All these threads use goby::middleware::Thread so they all have access to the goby::middleware::Thread::loop() method.

Writing thread-safe applications is now as a simple as ensuring that the various SimpleThread subclasses only share data via the publish/subscribe interface. This is easily accomplished by having each SimpleThread only access data within the class (no global or static variables) or data that has arrived via subscription callbacks.