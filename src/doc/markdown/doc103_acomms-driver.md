# goby-acomms: modemdriver (Driver to interact with modem firmware)

## Abstract class: ModemDriverBase
        
goby::acomms::ModemDriverBase defines the core functionality for an acoustic modem. It provides

* **A serial or serial-like (over TCP) reader/writer**. This is an instantiation of an appropriate derivative of the goby::util::LineBasedInterface class which reads the physical interface (serial or TCP) to the acoustic modem. The data (assumed to be ASCII lines offset by a delimiter such as NMEA0183 or the Hayes command set [AT]) are read into a buffer for use by the goby::acomms::ModemDriverBase derived class (e.g. goby::acomms::MMDriver). The type of interface is configured using a goby::acomms::protobuf::DriverConfig. The modem is accessed by the derived class using goby::acomms::ModemDriverBase::modem_start, goby::acomms::ModemDriverBase::modem_read, goby::acomms::ModemDriverBase::modem_write, and goby::acomms::ModemDriverBase::modem_close.
* **Signals** to be called at the appropriate time by the derived class. At the application layer, either bind the modem driver to a goby::acomms::QueueManager (goby::acomms::bind(goby::acomms::ModemDriverBase&, goby::acomms::QueueManager&) or connect custom function pointers or objects to the driver layer signals. 
* **Virtual functions**  for starting the driver (goby::acomms::ModemDriverBase::startup), running the driver (goby::acomms::ModemDriverBase::do_work), and initiating the transmission of a message (goby::acomms::ModemDriverBase::handle_initiate_transmission). The handle_initiate_transmission slot is typically bound to goby::acomms::MACManager::signal_initiate_transmission.

### Interacting with the goby::acomms::ModemDriverBase

To use the goby::acomms::ModemDriverBase, you need to create one of its implementations such as goby::acomms::MMDriver (WHOI Micro-Modem).

```
goby::acomms::ModemDriverBase* driver = new goby::acomms::MMDriver;
```

You will also need to configure the driver. At the very least this involves a serial port, baud, and modem ID (integer MAC address for the modem).

```
goby::acomms::protobuf::DriverConfig cfg;

cfg.set_serial_port("/dev/ttyS0");
cfg.set_modem_id(3);
```

Most modems will have specific other configuration that is required. For example the WHOI Micro-Modem NVRAM is set using three character strings followed by a number. This modem-specific configuration is stored as Protobuf extensions to goby::acomms::protobuf::DriverConfig, such as goby::acomms::micromodem::protobuf::config. If we were using the WHOI Micro-Modem and wanted to add an NVRAM configuration value we could write

```
cfg.MutableExtension(goby::acomms::micromodem::protobuf::config).add_nvram_cfg("DQF,1");
```

We need to connect any signals we are interested in. At a minimum this is goby::acomms::ModemDriverBase::signal_receive:

```
goby::acomms::connect(&driver->signal_receive, &handle_data_receive);
```

where handle_data_receive has the signature:
```
void handle_data_receive(const goby::acomms::protobuf::ModemTransmission& data_msg);
```

Next, we start up the driver with our configuration:

```
driver->startup(cfg);
```

We need to call goby::acomms::ModemDriverBase::do_work() on some reasonable frequency (greater than 5 Hz; 10 Hz is probably good). Whenever we need to transmit something, we can either directly call goby::acomms::ModemDriverBase::handle_initiate_transmission or connect goby::acomms::MACManager to do so for us on some TDMA cycle.

## Protobuf Message goby::acomms::protobuf::ModemTransmission

The goby::acomms::protobuf::ModemTransmission message is used for all outgoing (sending) and incoming (receiving) messages. The message itself only contains the subset of modem functionality that every modem is expected to support (point-to-point transmission of datagrams).

All other functionality is provided by [extensions](https://developers.google.com/protocol-buffers/docs/proto) to ModemTransmission such as those in mm_driver.proto for the WHOI Micro-Modem. These extensions provide access to additional features of the WHOI Micro-Modem (such as LBL ranging, two-way pings, and comprehensive receive statistics).

By making use of the Protobuf extensions in this way, Goby can both support unique features of a given modem while at that same time remaining general and agnostic to which modem is used when the features are shared (primarily data transfer).

## Writing a new driver

All of goby-acomms is designed to be agnostic of which physical modem is used. Different modems can be supported by subclassing goby::acomms::ModemDriverBase. You should check that a driver for your modem does not yet exist before attempting to create your own.

These are the requirements of the acoustic modem:

* it communicates using a line based text duplex connection using either serial or TCP (either client or server). NMEA0183 and AT (Hayes) protocols fulfill this requirement, for example. You can also write a driver that uses a different communication transport by implementing it directly in the driver rather than using the functionality in goby::acomms::DriverBase.
* it is capable of sending and verifying the accuracy (using a cyclic redundancy check or similar error checking) of fixed size datagrams (note that modems capable of variable sized datagrams also fit into this category).

Optionally, it can also support

* Acoustic acknowledgment of proper message receipt.
* Ranging to another acoustic modem or LBL beacons using time of flight measurements
* User selectable bit rates

The steps to writing a new driver include:

* Fully understand the basic usage of the new acoustic modem manually using minicom or other terminal emulator. Have a copy of the modem software interface manual handy.
* Figure out what type of configuration the modem will need. For example, the WHOI Micro-Modem is configured using string values (e.g. "SNV,1"). Extend goby::acomms::protobuf::DriverConfig to accomodate these configuration options. You will need to claim a group of extension field numbers that do not overlap with any of the drivers. The WHOI Micro-Modem driver goby::acomms::MMDriver uses extension field numbers 1000-1100 (see mm_driver.proto). You can read more about extensions in the official Google Protobuf documentation here: <https://developers.google.com/protocol-buffers/docs/proto>.
For example, if I was writing a new driver for the ABC Modem that needs to be configured using a few boolean flags, I might create a new message abc_driver.proto, make a note in driver_base.proto claiming extension number 1201.

* Subclass goby::acomms::ModemDriverBase and overload the pure virtual methods. Your interface should look like this:

\dontinclude abc_driver.h
\skip namespace
\until private
\skipline driver_cfg_
\until }
\until }
\until }
en
* Fill in the methods. You are responsible for emitting the goby::acomms::ModemDriverBase signals at the appropriate times. Read on and all should be clear.

```
goby::acomms::ABCDriver::ABCDriver()
{
  // other initialization you can do before you have your goby::acomms::DriverConfig configuration object
}
```

* At startup() you get your configuration from the application (e.g. pAcommsHandler)

\dontinclude abc_driver.cpp
\skipline startup
\until startup

* At shutdown() you should make yourself ready to startup() again if necessary and stop the modem:
\dontinclude abc_driver.cpp
\skipline shutdown
\until shutdown
* handle_initiate_transmission() is called when you are expected to initiate a transmission. It *may* contain data (in the ModemTransmission::frame field). If not, you are required to request data using the goby::acomms::ModemDriverBase::signal_data_request signal. Once you have data, you are responsible for sending it. I think a bit of code will make this clearer:
\dontinclude abc_driver.cpp
\skipline handle_initiate_transmission
\until handle_initiate_transmission
* Finally, you can use do_work() to do continuous work. You can count on it being called at 5 Hz or more (in pAcommsHandler, it is called on the MOOS AppTick). Here's where you want to read the modem incoming stream.
\dontinclude abc_driver.cpp
\skipline do_work
\until do_work

The full ABC Modem example driver exists in acomms/modemdriver/abc_driver.h and acomms/modemdriver/abc_driver.cpp. A simulator for the ABC Modem exists that uses TCP to mimic a very basic set of modem commands (send data and acknowledgment). To use the ABC Modem using the driver_simple example, run this set of commands (`socat` is available in most package managers or at <http://www.dest-unreach.org/socat/>):

```
1. run abc_modem_simulator running on same port (as TCP server)
> abc_modem_simulator 54321
2. create fake tty terminals connected to TCP as client to port 54321
> socat -d -d -v pty,raw,echo=0,link=/tmp/ttyFAKE1 TCP:localhost:54321
> socat -d -d -v pty,raw,echo=0,link=/tmp/ttyFAKE2 TCP:localhost:54321
3. start up driver_simple
> driver_simple /tmp/ttyFAKE1 1 ABCDriver
// wait a few seconds to avoid collisions
> driver_simple /tmp/ttyFAKE2 2 ABCDriver
```

Notes:
* See goby::acomms::MMDriver for an example real implementation.
* When a message is sent to goby::acomms::BROADCAST_ID (0), it should be broadcast if the modem supports such functionality. Otherwise, the driver should throw an goby::acomms::ModemDriverException indicating that it does not support broadcast allowing the user to reconfigure their MAC / addressing scheme.

## WHOI Micro-Modem Driver: MMDriver

### Supported Functionality 

The goby::acomms::MMDriver extends the goby::acomms::ModemDriverBase for the WHOI Micro-Modem acoustic modem. It is tested to work with revision 0.94.0.00 of the Micro-Modem 1 and revision 2.0.16421 of the Micro-Modem 2 firmware, but is known to work with older firmware (at least 0.92.0.85). It is likely to work properly with newer firmware, and any problems while using newer Micro-Modem firmware should be filed as a [bug in Goby](https://github.com/GobySoft/goby3/issues). The following features of the WHOI Micro-Modem are implemented, which comprise the majority of the Micro-Modem functionality:


* FSK (rate 0) data transmission
* PSK (rates 1,2,3,4,5) data transmission
* Narrowband transponder LBL ping
* REMUS transponder LBL ping
* User mini-packet 13 bit data transmission
* Two way ping
* Flexible Data Protocol (Micro-Modem 2 only)
* Transmit FM sweep
* Transmit M-sequence


### Micro-Modem NMEA to Goby ModemTransmission mapping

Mapping between modem_message.proto and mm_driver.proto messages and NMEA fields (see the MicroModem users guide at https://acomms.whoi.edu/micro-modem/software-interface/ for NMEA fields of the WHOI Micro-Modem):

Modem to Control Computer ($CA / $SN):
<table border=1>
<tr>
<th>NMEA talker</th>
<th>Mapping</th>
</tr>
<tr>
<td>$CACYC</td>
<td>
If we did not send $CCCYC, buffer data for $CADRQ by augmenting the provided ModemTransmission and calling signal_data_request:<br>
goby::acomms::protobuf::ModemTransmission.time() = goby::util::goby_time<uint64>() <br>
goby::acomms::protobuf::ModemTransmission.src() = ADR1<br>
goby::acomms::protobuf::ModemTransmission.dest() = ADR2<br>
goby::acomms::protobuf::ModemTransmission.rate() = Packet Type<br>
goby::acomms::protobuf::ModemTransmission.max_frame_bytes() = 32 for Packet Type == 0, 64 for Packet Type == 2, 256 for Packet Type == 3 or 5<br>
goby::acomms::protobuf::ModemTransmission.max_num_frames() = 1 for Packet Type == 0, 3 for Packet Type == 2, 2 for Packet Type == 3 or 8 for Packet Type == 5<br>
</td>
</tr> 
<tr>
<td>$CARXD</td>
<td>
only for the first $CARXD for a given packet (should match with the rest though): <br>
goby::acomms::protobuf::ModemTransmission.time() = goby::util::goby_time<uint64>() <br>
goby::acomms::protobuf::ModemTransmission.type()  = goby::acomms::protobuf::ModemTransmission::DATA <br>
goby::acomms::protobuf::ModemTransmission.src() = SRC<br>
goby::acomms::protobuf::ModemTransmission.dest() = DEST<br>
goby::acomms::protobuf::ModemTransmission.ack_requested() = ACK<br>
for each $CARXD: <br>
goby::acomms::protobuf::ModemTransmission.frame(F#-1) = goby::util::hex_decode(HH...HH) <br>
</td>
</tr>
<tr>
<td>$CAMSG</td>
<td>
Used only to detect BAD_CRC frames ($CAMSG,BAD_CRC...). 
(in extension goby::acomms::micromodem::protobuf::Transmission::frame_with_bad_crc) <br>
goby::acomms::micromodem::protobuf::frame_with_bad_crc(n) = Frame with BAD CRC (assumed next frame after last good frame). n is an integer 0,1,2,... indicating the nth reported BAD_CRC frame for this packet. (not the frame number)<br>
</td>
</tr>
<tr>
<td>$CAACK</td>
<td>
goby::acomms::protobuf::ModemTransmission.time() = goby::util::goby_time<uint64>() <br>
goby::acomms::protobuf::ModemTransmission.src() = SRC<br>
goby::acomms::protobuf::ModemTransmission.dest() = DEST<br>
(first CAACK for a given packet) goby::acomms::protobuf::ModemTransmission.acked_frame(0) = Frame#-1 (Goby starts counting at frame 0, WHOI starts with frame 1)<br>
(second CAACK for a given packet) goby::acomms::protobuf::ModemTransmission.acked_frame(1) = Frame#-1 <br>
(third CAACK for a given packet) goby::acomms::protobuf::ModemTransmission.acked_frame(2) = Frame#-1 <br>
...
</td>
</tr>
<tr>
<td>$CAMUA</td>
<td>
goby::acomms::protobuf::ModemTransmission.type() = goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC <br> 
 extension goby::acomms::micromodem::protobuf::Transmission::type = micromodem::protobuf::MICROMODEM_MINI_DATA <br>
goby::acomms::protobuf::ModemTransmission.time() = goby::util::goby_time<uint64>() <br>
goby::acomms::protobuf::ModemTransmission.src() = SRC<br>
goby::acomms::protobuf::ModemTransmission.dest() = DEST<br>
goby::acomms::protobuf::ModemTransmission.frame(0) = goby::util::hex_decode(HHHH) <br>
</td>
</tr>
<tr>
<td>$CAMPR</td>
<td>
goby::acomms::protobuf::ModemTransmission.time() = goby::util::goby_time<uint64>() <br>
goby::acomms::protobuf::ModemTransmission.dest() = SRC (SRC and DEST flipped to be SRC and DEST of $CCMPC)<br>
goby::acomms::protobuf::ModemTransmission.src() = DEST<br>
goby::acomms::protobuf::ModemTransmission.type() = goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC <br> 
 extension goby::acomms::micromodem::protobuf::Transmission::type = goby::acomms::micromodem::protobuf::MICROMODEM_TWO_WAY_PING <br>
(in extension goby::acomms::micromodem::Transmission::protobuf::ranging_reply) <br>
goby::acomms::micromodem::protobuf::RangingReply::one_way_travel_time(0) = Travel Time<br>
</td>
</tr>
<tr>
<td>$CAMPA</td>
<td>
goby::acomms::protobuf::ModemTransmission.time() = goby::util::goby_time<uint64>() <br>
goby::acomms::protobuf::ModemTransmission.src() = SRC<br>
goby::acomms::protobuf::ModemTransmission.dest() = DEST<br>
goby::acomms::protobuf::ModemTransmission.type() = goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC <br> 
 extension goby::acomms::micromodem::protobuf::Transmission::type = goby::acomms::micromodem::protobuf::MICROMODEM_TWO_WAY_PING <br>
</td>
</tr>
<tr>
<td>$SNTTA</td>
<td>
goby::acomms::protobuf::ModemTransmission.time() = hhmmsss.ss (converted to microseconds since 1970-01-01 00:00:00 UTC) <br>
goby::acomms::protobuf::ModemTransmission.time_source() = goby::acomms::protobuf::MODEM_TIME <br>
goby::acomms::protobuf::ModemTransmission.type() = goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC <br> 
 extension goby::acomms::micromodem::protobuf::Transmission::type =  micromodem::protobuf::MICROMODEM_REMUS_LBL_RANGING or micromodem::protobuf::MICROMODEM_NARROWBAND_LBL_RANGING (depending on which LBL type was last initiated)<br>
goby::acomms::protobuf::ModemTransmission.src() = modem ID<br>
(in extension goby::acomms::micromodem::protobuf::Transmission::ranging_reply) <br>
goby::acomms::micromodem::protobuf::RangingReply.one_way_travel_time(0) = TA<br>
goby::acomms::micromodem::protobuf::RangingReply.one_way_travel_time(1) = TB<br>
goby::acomms::micromodem::protobuf::RangingReply.one_way_travel_time(2) = TC<br>
goby::acomms::micromodem::protobuf::RangingReply.one_way_travel_time(3) = TD<br>
</td>
</tr>
<tr>
<td>$CAXST</td>
<td>
maps onto extension goby::acomms::micromodem::protobuf::Transmission::transmit_stat of type goby::acomms::micromodem::protobuf::TransmitStatistics. The two $CAXST messages (CYC and data) for a rate 0 FH-FSK transmission are grouped and reported at once.
</td>
</tr>
<tr>
<td>$CACST</td>
<td>
maps onto extension goby::acomms::micromodem::protobuf::Transmission::receive_stat of type micromodem::protobuf::ReceiveStatistics. The two $CACST messages for a rate 0 FH-FSK transmission are grouped and reported at once. Note that this message contains the one way time of flight for synchronous ranging (used instead of $CATOA). <br>
Also sets (which will <i>overwrite</i> goby_time() set previously): <br>
goby::acomms::protobuf::ModemTransmission.time() = TOA time (converted to microseconds since 1970-01-01 00:00:00 UTC) <br>
goby::acomms::protobuf::ModemTransmission.time_source() = goby::acomms::protobuf::MODEM_TIME <br>
</td>
</tr>
<tr>
<td>$CAREV</td>
<td>Not translated into any of the modem_message.proto messages. Monitored to detect excessive clock skew (between Micro-Modem clock and system clock) or reboot (INIT)</td>
</tr>
<tr>
<td>$CAERR</td>
<td>Not translated into any of the modem_message.proto messages. Reported to goby::glog.</td>
</tr>
<tr>
<td>$CACFG</td>
<td>
NVRAM setting stored internally.
</td>
</tr>
<tr>
<td>$CACLK</td>
<td>
Checked against system clock and if skew is unacceptable another $CCCLK will be sent. 
</td>
</tr>
<tr>
<td>$CADRQ</td>
<td>
Data request is anticipated from the $CCCYC or $CACYC and buffered. Thus it is not translated into any of the Protobuf messages.
</td>
</tr>
<tr>
<td>$CARDP</td>
<td>
goby::acomms::protobuf::ModemTransmission.type() = goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC <br> 
 extension goby::acomms::micromodem::protobuf::Transmission::type = micromodem::protobuf::MICROMODEM_FLEXIBLE_DATA <br>
goby::acomms::protobuf::ModemTransmission.src() = src<br>
goby::acomms::protobuf::ModemTransmission.dest() = dest<br>
goby::acomms::protobuf::ModemTransmission.rate() = rate<br>
goby::acomms::protobuf::ModemTransmission::frame(0) = goby::util::hex_decode(df1+df2+df3...dfN) where "+" means concatenate, unless any frame fails the CRC check, in which case this field is set to the empty string. <br>
micromodem::protobuf::frame_with_bad_crc(0) = 0 indicated that Goby frame 0 is bad, if any sub-frame in the FDP has a bad CRC<br>
</td>
</tr>
</table>

Control Computer to Modem ($CC):
<table border=1>
<tr>
<td>$CCTXD</td>
<td>
SRC = goby::acomms::protobuf::ModemTransmission..src()<br>
DEST = goby::acomms::protobuf::ModemTransmission.dest()<br>
A = goby::acomms::protobuf::ModemTransmission.ack_requested()<br>
HH...HH = goby::acomms::hex_encode(goby::acomms::protobuf::ModemTransmission::frame(n)), which n is an integer 0,1,2,... corresponding to the Goby frame that this $CCTXD belongs to.<br>
</td>
</tr>
<tr>
<td>$CCCYC</td>
<td>
Augment the ModemTransmission:<br>
goby::acomms::protobuf::ModemTransmission.max_frame_bytes() = 32 for Packet Type == 0, 64 for Packet Type == 2, 256 for Packet Type == 3 or 5<br>
goby::acomms::protobuf::ModemTransmission.max_num_frames() = 1 for Packet Type == 0, 3 for Packet Type == 2, 2 for Packet Type == 3 or 8 for Packet Type == 5<br>
If ADR1 == modem ID and frame_size() < max_frame_size(), buffer data for later $CADRQ by passing the ModemTransmission to signal_data_request<br>
CMD = 0 (deprecated field)<br>
ADR1 = goby::acomms::protobuf::ModemTransmission.src()<br>
ADR2 = goby::acomms::protobuf::ModemTransmission.dest()<br>
Packet Type = goby::acomms::protobuf::ModemTransmission.rate()<br>
ACK = if ADR1 == modem ID then goby::acomms::protobuf::ModemTransmission.ack_requested() else 1 <br> 
Nframes = goby::acomms::protobuf::ModemTransmission.max_num_frames()<br><br>
</td>
</tr> 
<tr>
<td>$CCCLK</td>
<td>Not translated from any of the modem_message.proto messages. (taken from the system time)</td>
</tr>
<tr>
<td>$CCCFG</td>
<td>Not translated from any of the modem_message.proto messages. (taken from values passed to the extension goby::acomms::micromodem::protobuf::Config::nvram_cfg of goby::acomms::protobuf::DriverConfig)</td>. If the extension goby::acomms::micromodem::protobuf::Config::reset_nvram is set to true, $CCCFG,ALL,0 will be sent before any other $CCCFG values.)
</tr>
<tr>
<td>$CCCFQ</td>
<td>Not translated from any of the modem_message.proto messages. $CCCFQ,ALL sent at startup.</td>
</tr>
<tr>
<td>$CCMPC</td>
<td>
goby::acomms::micromodem::protobuf::MICROMODEM_TWO_WAY_PING == extension goby::acomms::micromodem::protobuf::Transmission::type<br>
SRC = goby::acomms::protobuf::ModemTransmission.src()<br>
DEST = goby::acomms::protobuf::ModemTransmission.dest()<br>
</td>
</tr>
<tr>
<td>$CCPDT</td>
<td>
goby::acomms::micromodem::protobuf::protobuf::MICROMODEM_REMUS_LBL_RANGING == extension goby::acomms::micromodem::protobuf::Transmission::type<br>
micromodem::protobuf::REMUSLBLParams type used to determine the parameters of the LBL ping. The object provided with configuration (micromodem::protobuf::Config::remus_lbl) is merged with the object provided with the ModemTransmission (micromodem::protobuf::remus_lbl) with the latter taking priority on fields that a set in both objects: <br>
GRP = 1<br>
CHANNEL = modem ID % 4 + 1 (use four consecutive modem IDs if you need multiple vehicles pinging)<br>
SF = 0<br>
STO = 0<br>
Timeout = goby::acomms::micromodem::protobuf::REMUSLBLParams::lbl_max_range() m *2/ 1500 m/s * 1000 ms/s + goby::acomms::micromodem::protobuf::REMUSLBLParams::turnaround_ms() <br>
goby::acomms::micromodem::protobuf::REMUSLBLParams::enable_beacons() is a set of four bit flags where the least significant bit is AF enable, most significant bit is DF enable. Thus b1111 == 0x0F enables all beacons <br>
AF = goby::acomms::micromodem::protobuf::REMUSLBLParams::enable_beacons() >> 0 & 1<br>
BF = goby::acomms::micromodem::protobuf::REMUSLBLParams::enable_beacons() >> 1 & 1<br>
CF = goby::acomms::micromodem::protobuf::REMUSLBLParams::enable_beacons() >> 2 & 1<br>
DF = goby::acomms::micromodem::protobuf::REMUSLBLParams::enable_beacons() >> 3 & 1<br>
</td>
</tr>
<tr>
<td>$CCPNT</td>
<td>
goby::acomms::micromodem::protobuf::protobuf::MICROMODEM_NARROWBAND_LBL_RANGING == extension goby::acomms::micromodem::protobuf::Transmission::type<br>
goby::acomms::micromodem::protobuf::NarrowBandLBLParams type used to determine the parameters of the LBL ping. The object provided with configuration (goby::acomms::micromodem::protobuf::Config::narrowband_lbl) is merged with the object provided with the ModemTransmission (goby::acomms::micromodem::protobuf::narrowband_lbl) with the latter taking priority on fields that a set in both objects: <br>
<!--CCPNT, Ftx, Ttx, Trx, Timeout, FA, FB, FC, FD,Tflag-->
Ftx = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::transmit_freq() <br>
Ttx = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::transmit_ping_ms() <br>
Trx = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::receive_ping_ms() <br>
Timeout = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::lbl_max_range() m * 2/ 1500 m/s * 1000 ms/s + goby::acomms::micromodem::protobuf::NarrowBandLBLParams::turnaround_ms() <br>
FA = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::receive_freq(0) or 0 if receive_freq_size() < 1<br>
FB = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::receive_freq(1) or 0 if receive_freq_size() < 2<br>
FC = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::receive_freq(2) or 0 if receive_freq_size() < 3<br>
FD = goby::acomms::micromodem::protobuf::NarrowBandLBLParams::receive_freq(3) or 0 if receive_freq_size() < 4<br>
Tflag = micromodem::protobuf::NarrowBandLBLParams::transmit_flag() <br>
</td>
</tr>
<tr>
<td>$CCMUC</td>
<td>
SRC = goby::acomms::protobuf::ModemTransmission.src()<br>
DEST = goby::acomms::protobuf::ModemTransmission.dest()<br>
HHHH = goby::acomms::hex_encode(goby::acomms::protobuf::ModemTransmission::frame(0)) & 0x1F<br>
</td>
</tr>
<tr>
<td>$CCTDP</td>
<td>
dest = goby::acomms::protobuf::ModemTransmission.dest()<br>
rate = goby::acomms::protobuf::ModemTransmission.rate()<br>
ack = 0 (not yet supported by the Micro-Modem 2) <br>
reserved = 0 <br>
hexdata = goby::acomms::hex_encode(goby::acomms::protobuf::ModemTransmission::frame(0))<br>
</td>
</tr>
</table>

### Sequence diagrams for various Micro-Modem features using Goby

FSK (rate 0) data transmission
![](../images/goby-acomms-mmdriver-rate0.png)
\image latex goby-acomms-mmdriver-rate0.eps "FSK (rate 0) data transmission"


PSK (rate 2 shown, others are similar) data transmission
![](../images/goby-acomms-mmdriver-rate2.png)
\image latex goby-acomms-mmdriver-rate2.eps "PSK (rate 2 shown, others are similar) data transmission"


Narrowband transponder LBL ping
![](../images/goby-acomms-mmdriver-pnt.png)
\image latex goby-acomms-mmdriver-pnt.eps "Narrowband transponder LBL ping"


REMUS transponder LBL ping
![](../images/goby-acomms-mmdriver-pdt.png)
\image latex goby-acomms-mmdriver-pdt.eps "REMUS transponder LBL ping"

User mini-packet 13 bit data transmission
![](../images/goby-acomms-mmdriver-muc.png)
\image latex goby-acomms-mmdriver-muc.eps "User mini-packet 13 bit data transmission"


Two way ping
![](../images/goby-acomms-mmdriver-mpc.png)
\image latex goby-acomms-mmdriver-mpc.eps "Two way ping"

Flexible Data Protocol (Micro-Modem 2)
![](../images/goby-acomms-mmdriver-tdp.png)
\image latex goby-acomms-mmdriver-tdp.eps "Flexible Data Protocol"



## UDP Multicast Driver

The goby::acomms::UDPMulticastDriver provides an easy localhost testing interface as it implements ModemDriverBase for an Internet Protocol (IP) User Datagram Protocol (UDP) multicast transport.

For example, configure any number of modems running on a multicast enabled network:

```
modem_id: 1
driver_type: DRIVER_UDP_MULTICAST
[goby.acomms.udp_multicast.protobuf.config] {
    listen_address: "0.0.0.0"
    multicast_address: "239.142.0.10"
    multicast_port: 50031
    max_frame_size: 1400
}
```

## UDP Driver

The goby::acomms::UDPDriver is similar to the goby::acomms::UDPMulticastDriver but rather uses unicast UDP packets to an explicitly configured list of remote destinations. This is better suited when routing is involved.

Configuration merely involves setting the local udp port, and at least one remote endpoint (ip address, port, and modem id). For example, for a pair of modems (ids 1 and 2) running on localhost:

### Modem 1

```
modem_id: 1
driver_type: DRIVER_UDP
[goby.acomms.udp.protobuf.config] {
    local {
      port: 50001
    }
    remote {
      modem_id: 2
      ip: "127.0.0.1"
      port: 50002
    }
    max_frame_size: 1400
}
```

### Modem 2

```
modem_id: 2
driver_type: DRIVER_UDP
[goby.acomms.udp.protobuf.config] {
    local {
      port: 50002
    }
    remote {
      modem_id: 1
      ip: "127.0.0.1"
      port: 50001
    }
    max_frame_size: 1400
}
```

## Iridium Drivers

The goby::acomms::IridiumDriver was designed and testing on the Iridium 9523 for both RUDICS and short burst data (SBD). It may also work on other Iridium RUDICS and/or SBD enabled devices. It is intended to be used in companion with the goby::acomms::IridiumShoreDriver to handle DirectIP data and RUDICS in-bound (mobile-originated or MO) calls. Making calls from the shore station (mobile-terminated or MT) is not well supported by Iridium, and is thus not supported in Goby.

## Benthos Driver

The Benthos ATM900 series of acoustic modems is supported by the goby::acomms::BenthosATM900Driver using the Benthos CLAM shell and AT commands.

An example configuration might look like:
```
modem_id: 1
driver_type: DRIVER_BENTHOS_ATM900
connection_type: CONNECTION_SERIAL
serial_port: "/dev/ttyS0"
[goby.acomms.benthos.protobuf.config] {
        factory_reset: false
        start_timeout: 20
        max_frame_size: 128
        config: "@TxPower=8"
}
```
