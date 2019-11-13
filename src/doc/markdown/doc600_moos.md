# goby-moos: An overview of the Goby/MOOS interoperability library

`libgoby_moos` is an optional library that can be built when the [MOOS middleware core library](https://github.com/themoos/core-moos) is available. MOOS is a middleware with a long history in marine robotics. This Goby library provides functionality both to improve the use of MOOS, and to allow the Goby middleware to pass data to/from MOOS.

## iFrontSeat

iFrontSeat is a MOOS application used to interface a Goby/MOOS community (the "backseat") running pHelmIvP with a given manufacturer's vehicle (the "frontseat"). The usage of iFrontSeat and the existing driver suite is explained in the Goby user manual (see \ref main_resources). 

### Writing a new driver for iFrontSeat

#### Overview

iFrontSeat is intended to interface to a wide range of vehicles using any interface (e.g. proprietary extensions of NMEA-0183). The purpose of the driver is to implement the Goby FrontSeatInterfaceBase in the language of the particular frontseat vehicle system. Minimally, these are the requirements of the frontseat:

* it can provide a navigation solution for the vehicle (minimally latitude, longitude, depth, and speed), and typically also the geo-referenced pose of the vehicle (heading, pitch, yaw).
* it can accept a desired heading, speed, and depth (at around 1 Hz) for the vehicle and carry out these commands as quickly as reasonably possible given the vehicle's dynamics.

Additionally, the frontseat may provide or consume:

* arbitrary sensor data (e.g. CTD samples, acoustic modem datagrams)
* additional special commands (e.g. buoyancy adjustment, activate/deactivate sensors, low power mode) that the backseat can command of the frontseat.

#### State charts

The state of iFrontSeat (as shown in the following diagram) is determined by a combination of the state of the frontseat and the state of pHelmIvP. Only the state of the frontseat must be determined by each new driver, as the state of pHelmIvP is determined by code shared by all the drivers.

![](../images/state-diagram.png)
\image latex ../images/state-diagram.eps "State charts of the iFrontSeat interface and connected ends (pHelmIvP and frontseat)" width=0.9\textwidth

The **state of the frontseat** consists of two parallel state charts (command and data):

* Command state
    * FRONTSEAT_IDLE (required): The frontseat computer is alive and well, but is not running any mission (the vehicle is a standby mode).
    * FRONTSEAT_ACCEPTING_COMMANDS (required): The frontseat is accepting the backseat commands.
    * FRONTSEAT_NOT_CONNECTED (optional): No communication with the frontseat computer has been established (or a connection has been lost). If there is no way to tell whether the frontseat is alive at any given time, this state may not be implemented.
    * FRONTSEAT_IN_CONTROL (optional): The frontseat is running a mission and driving the vehicle but not accepting commands from the backseat. If the frontseat never runs missions without backseat control, this state may not be implemented.
* Data state (not diagrammed above)
    * frontseat_providing_data == true: The frontseat is providing all required data. What is required is determined by the specific driver, but at a minimum is the navigation solution.
    * frontseat_providing_data == false: The frontseat is not providing all required data.

The state transitions for the iFrontSeat interface states are (using the names as defined in the enumerations in moos/protobuf/frontseat.proto)

<table border=1>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Action</th>
  </tr>
  <tr>
    <td>Start</td>
    <td>INTERFACE_STANDBY</td>
    <td>Configuration Read</td>
  </tr>
  <tr>
    <td>INTERFACE_STANDBY</td>
    <td>INTERFACE_LISTEN</td>
    <td>frontseat_providing_data == true</td>
  </tr>
  <tr>
    <td>INTERFACE_LISTEN</td>
    <td>INTERFACE_COMMAND</td>
    <td>FRONTSEAT_ACCEPTING_COMMANDS && HELM_DRIVE</td>
  </tr>
  <tr>
    <td>INTERFACE_COMMAND</td>
    <td>INTERFACE_LISTEN</td>
    <td>(FRONTSEAT_IN_CONTROL || FRONTSEAT_IDLE) && HELM_DRIVE</td>
  </tr>
  <tr>
    <td>INTERFACE_COMMAND</td>
    <td>INTERFACE_HELM_ERROR</td>
    <td>HELM_NOT_RUNNING || HELM_PARK</td>
  </tr>
  <tr>
    <td>INTERFACE_LISTEN || INTERFACE_COMMAND</td>
    <td>INTERFACE_HELM_ERROR</td>
    <td>HELM_PARK || if (helm_enabled) HELM_NOT_RUNNING (after timeout)</td>
  </tr>
  <tr>
    <td>INTERFACE_LISTEN || INTERFACE_COMMAND</td>
    <td>INTERFACE_FS_ERROR</td>
    <td>FRONTSEAT_NOT_CONNECTED || frontseat_providing_data == false</td>
  </tr>
  <tr>
    <td>INTERFACE_STANDBY</td>
    <td>INTERFACE_FS_ERROR</td>
    <td>FRONTSEAT_NOT_CONNECTED (after timeout) </td>
  </tr>
  <tr>
    <td>INTERFACE_HELM_ERROR</td>
    <td>INTERFACE_STANDBY</td>
    <td>HELM_DRIVE</td>
  </tr>
  <tr>
    <td>INTERFACE_FRONTSEAT_ERROR</td>
    <td>INTERFACE_STANDBY</td>
    <td>(if(ERROR_FRONTSEAT_NOT_CONNECTED) !FRONTSEAT_NOT_CONNECTED) || (if(ERROR_FRONTSEAT_NOT_PROVIDING_DATA) frontseat_providing_data == true) </td>
  </tr>
</table>

#### Example "ABC" driver

We will show you how to a write a new driver by example. To do so, we have created a simple frontseat simulator ("abc_frontseat_simulator") that is intended to represent the real vehicle frontseat control system. The full source code for this example is given at:

* components/moos/abc_frontseat_driver/abc_frontseat_driver.h
* components/moos/abc_frontseat_driver/abc_frontseat_driver.cpp
* components/moos/abc_frontseat_driver/abc_frontseat_driver_config.proto

A complete production driver is provided by BluefinFrontSeat for the Bluefin Robotics AUVs that conform to the Bluefin Standard Payload Interface version 1.8 and newer.

The transport for the ABC frontseat is TCP: the simulator (frontseat) listens on a given port and the driver connects to that machine and port. The wire protocol is a simple ascii line-based protocol where lines are terminated by carriage-return and newline (`<CR><NL>` or "\r\n"). Each message has a name (key), followed by a number of comma-delimited, colon-separated fields:

<table border=1>
  <tr>
    <th>Key</th>
    <th>Description</th>
    <th>Direction (relative to frontseat)</th>
    <th>Format</th>
    <th>Example</th>
  </tr>
  <tr>
    <td>START</td>
    <td>Simulator initialization message</td>
    <td>Receive</td>
    <td>START,LAT:{latitude decimal degrees},LON:{longitude decimal degrees},DURATION:{simulation duration seconds}</td>
    <td>START,LAT:42.1234,LON:-72,DURATION:600</td>
  </tr>
  <tr>
    <td>CTRL</td>
    <td>Frontseat state message</td>
    <td>Transmit</td>
    <td>CTRL,STATE:{PAYLOAD (if backseat control) or IDLE}</td>
    <td>CTRL,STATE:PAYLOAD</td>
  </tr>
  <tr>
    <td>NAV</td>
    <td>Navigation message generated from very primitive dynamics model (depth & heading changes are instantaneous)</td>
    <td>Transmit</td>
    <td>NAV,LAT:{latitude decimal degrees},LON:{longitude decimal degrees},DEPTH:{depth in meters},HEADING:{heading in degrees},SPEED:{speed in m/s}</td>
    <td>NAV,LAT:42.1234,LON:-72.5435,DEPTH:200,HEADING:223,SPEED:1.4</td>
  </tr>
  <tr>
    <td>CMD</td>
    <td>Desired course command from backseat</td>
    <td>Receive</td>
    <td>CMD,HEADING:{desired heading in degrees},SPEED:{desired speed in m/s},DEPTH:{desired depth in m}</td>
    <td>CMD,HEADING:260,SPEED:1.5,DEPTH:100</td>
  </tr>
  <tr>
    <td>CMD</td>
    <td>Reponse to last CMD</td>
    <td>Transmit</td>
    <td>CMD,RESULT:{OK or ERROR}</td>
    <td>CMD,RESULT:OK</td>
  </tr>
</table>

Your driver will be (at a minimum) a C linkage function "frontseat_driver_load" and a subclass of goby::moos::FrontSeatInterfaceBase. It should be compiled into a shared library (.so on Linux).

The C function is used by iFrontSeat to load your driver:

```
extern "C"
{
    goby::moos::FrontSeatInterfaceBase* frontseat_driver_load(goby::apps::moos::protobuf::iFrontSeatConfig* cfg)
    {
        return new AbcFrontSeat(*cfg);
    }
}
```

First you should decide what configuration your driver will accept. Your configuration object is an extension to the Google Protobuf message "iFrontSeatConfig". For the ABC frontseat driver, we use the abc_frontseat_driver_config.proto file to define the configuration:

\include abc_frontseat_driver_config.proto

In this case, we need to know what IP address and TCP port the abc_frontseat_simulator is listening on, and the starting position of the simulator.

Next, you should fill out the virtual methods of goby::moos::FrontSeatInterfaceBase:

* The method "frontseat_state" reports the driver's belief of the frontseat command state.

```
goby::moos::protobuf::FrontSeatState AbcFrontSeat::frontseat_state() const
{
    return frontseat_state_;
}
```

In this case, we set the value of frontseat_status_ based on the received "CTRL" messages:

```
    if (parsed["KEY"] == "CTRL")
    {
        if (parsed["STATE"] == "PAYLOAD")
            frontseat_state_ = gpb::FRONTSEAT_ACCEPTING_COMMANDS;
        else if (parsed["STATE"] == "AUV")
            frontseat_state_ = gpb::FRONTSEAT_IN_CONTROL;
        else
            frontseat_state_ = gpb::FRONTSEAT_IDLE;
    }
```

* The method "frontseat_providing_data" reports the frontseat's data state (see \ref moos_ifs_new_driver_state). It must return true if the frontseat is providing data to the driver reasonably often (where reasonable is defined by the driver). Here we set the class member variable "frontseat_providing_data_" to true each time we get a "NAV" message, and then false if we have had no "NAV" messages in the last 10 seconds.

```
bool AbcFrontSeat::frontseat_providing_data() const
{
    return frontseat_providing_data_;
}
```

* The method "send_command_to_frontseat" is called whenever iFrontSeat needs to send a command to the frontseat. This command typically contains a desired heading, speed, and depth, but could alternatively contain a special command defined via an extension to the goby::moos::protobuf::CommandRequest message.

```
void AbcFrontSeat::send_command_to_frontseat(const gpb::CommandRequest& command)
{
    if (command.has_desired_course())
    {
        std::stringstream cmd_ss;
        const goby::moos::protobuf::DesiredCourse& desired_course = command.desired_course();
        cmd_ss << "CMD,"
               << "HEADING:" << desired_course.heading() << ","
               << "SPEED:" << desired_course.speed() << ","
               << "DEPTH:" << desired_course.depth();
        write(cmd_ss.str());
        last_request_ = command;
    }
    else
    {
        glog.is(VERBOSE) && glog << "Unhandled command: " << command.ShortDebugString()
                                 << std::endl;
    }
}
```

* The method "send_data_to_frontseat" is called whenever iFrontSeat needs to send data to the frontseat. These data could include sensor readings from instruments that are directly connected to the backseat, such as a CTD or acoustic modem. Our bare-bones example frontseat doesn't require any data from the backseat, so we just leave an empty implementation here.

```
void AbcFrontSeat::send_data_to_frontseat(const gpb::FrontSeatInterfaceData& data)
{
    // ABC driver doesn't have any data to sent to the frontseat
}
```

* The method "send_raw_to_frontseat" is called whenever an external application wants to directly control the frontseat. This can be left blank (or post a warning to the glog) if there is no need (or desire) to allow for direct control of the frontseat from external applications.
* The method "loop" is called regularly (at the AppTick of iFrontSeat) and is where you can read data from the frontseat and do other regular work.

```
void AbcFrontSeat::loop()
{
    check_connection_state();
    try_receive();
    // if we haven't gotten data for a while, set this boolean so that the
    // FrontSeatInterfaceBase class knows
    if (goby_time<double>() > last_frontseat_data_time_ + allowed_skew)
        frontseat_providing_data_ = false;
}
```

Now, the final task is to call the appropriate signals in FrontSeatInterfaceBase upon receipt of data and responses to commands. The signals are called just like normal functions with the corresponding signatures. These signals (except signal_raw_to_frontseat) are typically called in response to data received in the loop() method.

* signal_data_from_frontseat: Call when a navigation solution is received from the frontseat. This may have to be merged from several messages, which is why goby::moos::protobuf::NodeStatus has the *_time_lag fields. These fields can be used to indicate the offset of certain fields from the timestamp on the message. You can use the FrontSeatInterfaceBase::compute_missing to compute the loocal fix (X, Y, Z) from the global fix (latitude, longitude, depth) or vice-versa.
* signal_command_response: Call when the frontseat acknowledges a command, if the command request includes response_requested == true. Include the success or failure of the command, and an error code (with description) if applicable. 
* signal_raw_from_frontseat: Call when a raw message (e.g. "CMD,RESULT:OK") is received from the frontseat. This is for logging and debug purposes.
* signal_raw_to_frontseat: Call when a raw message (e.g. "CMD,HEADING:260,SPEED:1.5,DEPTH:100") is send to the frontseat. This is for logging and debug purposes.

For testing the ABC driver to see how it functions, you will need to run 

```
abc_frontseat_simulator 54321
```
where 54321 is the port for the simulator to listen on. 

Then, run iFrontSeat in a MOOS community with pHelmIvP with the following configuration:

```
ProcessConfig = iFrontSeat_bluefin
{
    common { 
        verbosity: DEBUG1
    }
    [abc_config] {  #  (optional)
      tcp_address: "localhost"  #  (required)
      tcp_port: 54321  #  (optional) (default=54321)
      start { lat: 44.0888889 lon: 9.84861111 duration: 600 }
    }
}
```

You can change the start position as desired.
