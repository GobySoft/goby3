# goby-middleware: Standalone Applications

These applications are written using the Goby Application framework, but are standalone from any *interprocess* layer communications.

For useful applications that use the *interprocess* layer publish/subscribe infrastructure, see the 
- [ZeroMQ Applications](doc501_zeromq_applications.md) page

## goby_store_server

`goby_store_server` is a SQLite-based store-and-forward database for the `driver_type: DRIVER_STORE_SERVER` driver (run in `gobyd`, `goby_intervehicle_portal`, `goby_modemdriver` or `pAcommsHandler`). It opens a TCP socket and creates an SQLite database. It then handles connections for outbound messages that are written to the database, for later retrieval by the destination client.

It has simple configuration:

- tcp_server 
	- bind_port: A TCP port to bind on - must match the TCP port given to the DRIVER_STORE_SERVER configuration.
	- db_file_dir: Path to a directory to store the SQLite database files.


## goby_rockblock_simulator

A simulator for the RockBLOCK series of Iridium modems. This can be used to simulate SBD messages for the DRIVER_IRIDIUM and DRIVER_IRIDIUM_SHORE drivers.

Configuration:

- imei_to_id: Mapping between IMEI number and Goby Modemdriver ID.
- mo_http_server: Server to connect to for Mobile Originated (MO) messages. This would typically be the Goby Driver's port.
- mt_http_server_port: Bind port for Mobile Terminated messages.

## goby_basic_frontseat_simulator

A primitive dynamics model backs a simple text-based command protocol to create `goby_basic_frontseat_simulator`. The point of this simulator is to demonstrate the `goby_frontseat_interface` plugin infrastructure and provide a low-fidelity vehicle simulator for testing missions where accurate kinematics is unnecessary.

To run:
```
goby_basic_frontseat_simulator [tcp listen port]
```

It can be connected to using the `goby_frontseat_interface_basic_simulator` shell script that runs `goby_frontseat_interface` using the `libgoby_frontseat_basic_simulator.so` plugin.

## goby_liaison_standalone

A version of `goby_liaison` used for running tabs that do not need publish/subscribe communications. This means it doesn't require a running `gobyd`. See the [goby_liaison](doc505_goby_liaison.md) page for more details on `goby_liaison`. The standalone version cannot meaningfully run Commander or Scope and so is only useful for custom tabs that don't need *interprocess* communications. See the Jaiabot pre-launch plugin for a use case: https://github.com/jaiarobotics/jaiabot/tree/1.y/src/lib/liaison/prelaunch

## goby_serial_mux

Splits a single "primary" serial port into multiple "secondary" pseudoterminals (virtual serial ports). All secondary ports will receive a copy of data read from the primary serial port. All data written to the secondary ports will be multiplexed and sent out the primary serial port, unless 'allow_write: false' is specified for the secondary ports.

This is useful, for example, for sharing a GPS serial feed between multiple applications that need it and all require serial connections. In the GPS case, `gpsd` is an alternative option.

