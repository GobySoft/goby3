# goby-zeromq: Goby Visualization Interfaces

A key part of operating autonomous marine vehicles is the ability to visualize their locations. Goby provides some interfaces to existing visualization tools suitable for displaying marine vehicles. 

## goby_geov_interface

The GEOV project: https://github.com/GobySoft/geov lets you run a local server that provides data to Google Earth Pro. 

The `goby_geov_interface` provides a client to the GEOV server to input vehicle position data from Goby3.

The key subscription is:

```
- group: goby::middleware::frontseat::node_status
  scheme: PROTOBUF
  type: goby::middleware::frontseat::protobuf::NodeStatus
```

Using the data from `node_status`, the correct MySQL insert is performed for GEOV.

Configuration variables are:

- simulation: If true, writes to GEOV as a simulation data source (default: false)
- mysql_host: IP Address or domain name for GEOV MYSQL server 
- mysql_user: User name for GEOV input
- mysql_password: Password for GEOV input
- mysql_port: Port for GEOV MYSQL server
- position_report_interval: Seconds between position reports to enter into GEOV for a given vehicle (default: 1)


## goby_opencpn_interface

[OpenCPN](https://www.opencpn.org/) is an open source Chart Plotter Navigation software. This allows you to easily display other vessels, support vessel GPS position, ENC and RNC nautical charts, etc.

The `goby_opencpn_interface` allows you to input autonomous vehicle positions into OpenCPN as if they are AIS positions, without having to modify OpenCPN to do so.

### OpenCPN setup

Install OpenCPN:
```
sudo add-apt-repository ppa:opencpn/opencpn
sudo apt-get update
sudo apt-get install opencpn
```

Add charts (for example all of Massachusetts):
```
    Options: Charts
        Chart Downloader
            Add Catalog
            USA - NOAA & Inland Charts
                ENC -> by States -> MA
                Update
                Download Charts…
                    Download selected charts
            Apply
        Chart Files
            Prepare All ENC Charts
```

Add connection:

```
    Options: Connections
        Add connection : Network
        TCP: 127.0.0.1 Port 54000
```

Show AIS target tracks and names:
```
    Options: Ships: AIS Targets
        Check: Show target tracks, length (min): 20
        Check: Show names with AIS Targets at scale greater than 1: 250000
```

### goby_opencpn_interface Configuration

The only significant configuration setting required is 

```
ais_server {
    bind_port: 54000
    set_reuseaddr: true
}
```

Where the `bind_port` needs to match the port configured in the Connections tab of OpenCPN (previous section).

### Publish/subscribe

The key subscription is the same as `goby_geov_interface`:

```
- group: goby::middleware::frontseat::node_status
  scheme: PROTOBUF
  type: goby::middleware::frontseat::protobuf::NodeStatus
```

From the `node_status`, `goby_opencpn_interface` creates spoof AIS messages for this vehicle and sends them to all connected clients (OpenCPN in this case).

`goby_geov_interface` can also publish two variables:

```
- group: goby::middleware::opencpn::route
  scheme: PROTOBUF
  type: goby::middleware::protobuf::Route
- group: goby::middleware::opencpn::waypoint
  scheme: PROTOBUF
  type: goby::middleware::protobuf::Waypoint
```

These are published if you create a Route or Waypoint, respectively, in OpenCPN and then right click on it, choose "Send to GPS" and send it to the same port as `goby_opencpn_interface` (in the example above that would be "TCP:127.0.0.1:54000").

These publications can then be used to map onto data fields in `goby_liaison`, using the "External Data" functionality. This allows you to send positions and routes using the OpenCPN mapping software rather than manually entering latitude/longitude values into `goby_liaison` Commander.