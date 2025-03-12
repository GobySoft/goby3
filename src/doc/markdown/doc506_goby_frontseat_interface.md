# goby-zeromq: goby_frontseat_interface

The `goby_frontseat_interface` application provides a common application for handling plugins for different vehicle control computers (Bluefin, Iver, WaveGlider, etc.). The goal is to reduce the duplication of effort required to support a new vehicle platform with an autonomy system written in Goby3.

This is the Goby version of the [MOOS `iFrontSeat`](doc600_moos.md). The two applications share the same drivers and motivation, so it is worth reading the iFrontSeat documentation as well.

`goby_frontseat_interface` is effectively a mediator between two interfaces: the Helm Interface (providing desired setpoints of heading, speed, depth, etc.) and the Vehicle Interface (carrying out the desired setpoints and providing low-level data).

## Publish/subscribe


The groups are defined in
```
#include <goby/middleware/frontseat/groups.h>
```

and the Protobuf messages are in [`goby/src/middleware/protobuf/frontseat.proto`](https://github.com/GobySoft/goby3/blob/3.0/src/middleware/protobuf/frontseat.proto) and can be included using:

```
#include <goby/middleware/protobuf/frontseat.pb.h>
```

### API Diagram

![goby_clang_tool generated API figure](images/goby_frontseat_interface_stub_deployment.png)


## Helm Interface

The Helm Interface must provide the following publications:

1. Group: **goby::middleware::frontseat::groups::desired_course**,
Message: *goby::middleware::frontseat::protobuf::DesiredCourse*: Desired heading, speed, depth, etc. for the vehicle.
2. Group: **goby::middleware::frontseat::groups::helm_state**, Message: *goby::middleware::frontseat::protobuf::HelmStateReport*: State of the Helm (either HELM_DRIVE for the Helm is ready to drive or HELM_PARK for the Helm is in a standby mode).

It must also subscribe to:

1. Group: **goby::middleware::frontseat::groups::node_status**,
Message: *goby::middleware::frontseat::protobuf::NodeStatus*: Position, orientation, etc. of the vehicle.

`goby_frontseat_interface` has built-in support for the pHelmIvP application from MOOS-IvP or alternatively the user can provide this interface with a custom app.

### MOOS-IvP 
To use the MOOS-IvP interface, complete the `[goby.moos.protobuf.moos_helm]` configuration block:

```
[goby.moos.protobuf.moos_helm] {
  server: "localhost" # MOOSDB address
  port: 9000          # MOOSDB port
}
```
and at a minimum run MOOSDB and pHelmIvP.

## Vehicle Interface

The Vehicle Interface connects the vehicle API to Goby3. It is a subclass of `goby::middleware::frontseat::InterfaceBase` which is defined in

```
#include <goby/middleware/frontseat/interface.h>
```

See the [MOOS `iFrontSeat`](doc600_moos.md) document on writing this interface. 

The Vehicle Interface must be compiled as a shared library that exposes a C function "frontseat_driver_load" which must have the signature:
```
extern "C"
{
    goby::middleware::frontseat::InterfaceBase* frontseat_driver_load(goby::middleware::frontseat::protobuf::Config* cfg)
    {
        return new goby::middleware::frontseat::Iver(*cfg);
    }
}
```

This shared library is then loaded at runtime by `goby_frontseat_interface` using the environmental variable FRONTSEAT_DRIVER_LIBRARY, which is a path to a shared library containing the desired interface to run. This path must be fully qualified or on the `ld` library path (e.g. environmental variable `LD_LIBRARY_PATH`).

For example:

```
export FRONTSEAT_DRIVER_LIBRARY=/home/toby/opensource/goby3/build/lib/libgoby_frontseat_basic_simulator.so.30
goby_frontseat_interface goby_frontseat_interface.pb.cfg
```

## Configuration

The `goby_frontseat_interface` has one required configuration block:

- frontseat_cfg: Passed to the frontseat driver
	+ type: Vehicle type
	+ require_helm: Set false if the vehicle can run missions without a helm (Default: true)
	+ *name*: Omit: it is automatically set to `interprocess.platform`. This configuration variable exists here  exists for iFrontSeat support.
	+ *origin*: Omit: it is automatically set to `app.geodesy.lat/lon_origin`. This configuration variable exists here for iFrontSeat support.
	
You can set `[goby.moos.protobuf.moos_helm]` if using MOOS-IvP; see above.

## Available drivers in Goby3

### Basic Simulator

This driver shows how to use `goby_frontseat_interface` and provides an example for creating your own plugins. It can also be used for simple vehicle simulations.

This is run with the `goby_basic_frontseat_simulator` application which provides the vehicle interface and a basic motion model.

Additional configuration:

- frontseat_cfg
	+ `[goby.middleware.frontseat.protobuf.basic_simulator_config]`: Configuration when running the basic simulator plugin.
		* tcp_address: Address of simulator
		* tcp_port: Port of simulator
		* start: Start location/pose of simulator.

See the `goby_frontseat_interface` configuration from the Goby3 course for a simple working example: https://github.com/GobySoft/goby3-course/blob/master/launch/alpha/usv_config/frontseat.pb.cfg, launched from https://github.com/GobySoft/goby3-course/blob/master/launch/alpha/usv.launch.

### Bluefin

The `goby_frontseat_interface_bluefin` shell script runs `goby_frontseat_interface` with the plugin for the [General Dynamics Bluefin](https://gdmissionsystems.com/underwater-vehicles/bluefin-robotics) [Standard Payload Interface](https://gdmissionsystems.com/-/media/General-Dynamics/Maritime-and-Strategic-Systems/Bluefin/PDF/Payload-Interface-Specification-Download.ashx?la=en&hash=7A8197673ED8911E07821B48CB4AF473) (SPI). This public SPI is not complete, so request a copy from Bluefin if possible.

Key additional configuration variables:

- frontseat_cfg:
	- `[goby.middleware.frontseat.protobuf.bluefin_config]`
		+ huxley_tcp_address: Address for Huxley (Bluefin computer)
		+ huxley_tcp_port: Port for Huxley

See `goby_frontseat_interface_bluefin -e` for more configuration options.

### Iver

The `goby_frontseat_interface_iver` shell script runs `goby_frontseat_interface` with the plugin for the [L3Harris Iver](https://www.l3harris.com/all-capabilities/iver-suite-autonomous-underwater-vehicles) AUV using the Iver "Remote Helm" interface.



Key additional configuration variables:

- frontseat_cfg:
	- `[goby.middleware.frontseat.protobuf.iver_config]`
		+ serial_port: Serial port connected to Remote Helm
		+ serial_baud: Baud for serial_port
		+ remote_helm_version_major: Major version of Remote Helm (e.g., 5)


See `goby_frontseat_interface_iver -e` for more configuration options.


### WaveGlider SV2 

This is largely obsolete at this point given the end of life for the WaveGlider SV2, but the `goby_frontseat_interface_waveglider` shell script runs a plugin for the LiquidRobotics WaveGlider SV2.  


Key additional configuration variables:

- frontseat_cfg:
	- `[goby.middleware.frontseat.protobuf.waveglider_sv2_config]`:
		- pm_serial_port: Serial port connected to the SV2 PM.
	    - pm_serial_baud: Baud for serial port
	    - board_id: 3
	    - task_id: 1

