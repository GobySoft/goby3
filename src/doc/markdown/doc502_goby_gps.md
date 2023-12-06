# goby-zeromq: goby_gps

The `goby_gps` application publishes data from the widely used GPS server, `gpsd`.  It does not directly read data from the NMEA-0183 serial stream of the GPS. While this slightly increase the complication of setup, the benefits of serving GPS data using `gpsd` usually outweighs the drawback of this additional application. For example, NTP and Chronyd can directly receive GPS data from `gpsd` and correct the system time based on the GPS time.

## gpsd

gpsd has [comprehensive documentation](https://gpsd.gitlab.io/gpsd/gpsd.html) of its own, so there's no need to duplicate that information here.

For Debian/Ubuntu systems, setting up gpsd is usually as simple as:

```
sudo apt install gpsd
```

and then editing `/etc/default/gpsd` to specify your GPS devices. For example, if your GPS is connected to the serial port `/dev/ttyS0`, you would change the DEVICES line to read:

```
DEVICES="/dev/ttyS0"
```

Then reboot or do a 

```
sudo systemctl restart gpsd
```

It is very helpful to check that GPS data is correctly coming into `gpsd` using one of the various tools, such as `cgps`:

```
sudo apt install  gpsd-tools
cgps
```

If you see valid time, latitude, longitude, etc. data then gpsd is set up correctly. If not, check your serial port and perhaps directly examine the NMEA traffic using `minicom` or similar.

## goby_gps

`goby_gps` is a client for GPSD using the [TCP / JSON protocol](https://gpsd.gitlab.io/gpsd/gpsd_json.html).

Upon startup `goby_gps` connects to `gpsd` using a TCP client and then sends a `WATCH` command to `gpsd` to enable streaming of GPS data. 

`goby_gps` handles three types of `gpsd` data: TPV (time-position-velocity), SKY (sky view of the GPS satellites), and ATT (vehicle attitude). These are converted from JSON to equivalent Protocol Buffers messages and published on the ZeroMQ interprocess layer.

| GPSD Message | Protobuf Message | Goby Group |
|------------------------|-----------------------------|-----------------------|
| TPV                      |  goby::middleware::protobuf::gpsd::TimePositionVelocity | goby::middleware::groups::gpsd::tpv |
| SKY                      | goby::middleware::protobuf::gpsd::SkyView | goby::middleware::groups::gpsd::sky|
| ATT                      | goby::middleware::protobuf::gpsd::Attitude | goby::middleware::groups::gpsd::att|

The groups are defined in:

```
#include <goby/middleware/gpsd/groups.h>
```

and the Protobuf messages are in [`goby/src/middleware/protobuf/gpsd.proto`](https://github.com/GobySoft/goby3/blob/3.0/src/middleware/protobuf/gpsd.proto) and can be included using:

```
#include <goby/middleware/protobuf/gpsd.pb.h>
```

If you're only interested in position data, you can simply subscribe to the TPV data from another ZeroMQ-based application:

```
#include <goby/middleware/gpsd/groups.h>
#include <goby/middleware/protobuf/gpsd.pb.h>

//...

interprocess().subscribe<goby::middleware::groups::gpsd::tpv>(
        [this](const goby::middleware::protobuf::gpsd::TimePositionVelocity& tpv) {
        std::cout << "Latitude: " << tpv.location().lat() << std::endl;
        std::cout << "Longitude: " << tpv.location().lon() << std::endl; 
 });
```