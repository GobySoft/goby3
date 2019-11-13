# Goby Underwater Autonomy Project

![](../images/gobysoft_logo_image_only_medium.png)

The Goby Underwater Autonomy Project aims to create a unified framework for autonomous marine vehicle collaboration, seamlessly incorporating efficient intervehicle (acoustic, satellite, etc.), flexible interprocess (ethernet, local sockets, etc.), and intuitive interthread (shared pointer) communications. In addition, Goby provides a variety of useful tools for interacting with other marine-related middlewares, as well as marine engineering and oceanographic data. The Goby libraries are licensed under the [GNU Lesser General Public License](http://www.gnu.org/licenses/lgpl.html) and the applications (binaries) are licensed under the [GNU General Public License](http://www.gnu.org/licenses/gpl.html).

## Resources

  * Home page, code, bug tracking, and wiki: https://github.com/GobySoft/goby3.
  * Examples: <https://github.com/GobySoft/goby3-examples>
  * User Manual: [(pdf)](http://gobysoft.org/dl/goby3-user-manual.pdf)
  * Developers' Manual: [(html)](http://gobysoft.org/doc/3.0) [(pdf)](http://gobysoft.org/dl/goby3-dev.pdf)
  * Both user and developer documentation for a particular release as a Debian package:

    ```apt install goby3-doc```

## Developer manual

Goby is comprised of several libraries:

  * Core library (`libgoby.so`)
  * ZeroMQ support library (`libgoby_zeromq.so`) - optional
  * MOOS support library (`libgoby_moos.so`) - optional

Along with each of those libraries, Goby provides a number of related applications.

### Core library

The core `libgoby.so` is comprised of several conceptual components:

  * [acomms](doc100_acomms.md) - tackle the extremely rate limited acoustic networking problem. This part of Goby was designed with modules that can operate independently for a developer looking to integrate a specific component (e.g. just encoding/decoding) without committing to the entire goby-acomms stack.
  * [middleware](doc200_middleware.md) - (*new for Goby 3*) nested publish/subscribe middleware based on interthread, interprocess, and intervehicle communications.
  * [util](doc300_util.md) - provide utility functions for tasks such as logging, scientific calculations, string parsing, and serial device i/o. Goby also relies on the [Boost](http://www.boost.org) libraries for many utility tasks to fill in areas where the C++ Standard Library is insufficient or unelegant.

### ZeroMQ support library

  * [zeromq](doc500_zeromq.md) - implementation of the Goby3 interprocess portal using the [ZeroMQ](https://zeromq.org/) transport library.

### MOOS support library

  * [moos](doc600_moos.md) - classes, applications (e.g. pAcommsHandler and iFrontSeat), and functions for interoperating between Goby and the [MOOS](https://github.com/themoos/core-moos) middleware.

## Publications

  * T. Schneider, [Goby3: A new open-source middleware for nested communication on autonomous marine vehicles](http://gobysoft.org/dl/schneider-auv-2016-goby3.pdf). IEEE AUV 2016 / Tokyo.
  * T. Schneider and H. Schmidt, [The Dynamic Compact Control Language: A Compact Marshalling Scheme for Acoustic Communications](http://gobysoft.org/dl/dccl_oceans10.pdf). IEEE OCEANS'10 / Sydney.
  * T. Schneider and H. Schmidt, [Goby-Acomms: A modular acoustic networking framework for short-range marine vehicle communication](http://gobysoft.org/dl/goby-acomms1.pdf). Unpublished working paper.
  * T. Schneider and H. Schmidt, [Goby-Acomms version 2: extensible marshalling, queuing, and link layer interfacing for acoustic telemetry](http://gobysoft.org/dl/mcmc2012_goby2.pdf). 9th IFAC Conference on Manoeuvring and Control of Marine Craft '12 / Arenzano, Italy.
  * T. Schneider, [Advances in Integrating Autonomy with Acoustic Communications for Intelligent Networks of Marine Robots](http://gobysoft.org/dl/schneider-toby-final-phd-thesis-online.pdf). PhD Thesis, MIT/WHOI Joint Program.

## Download and Install Goby

### Debian and Ubuntu

The only officially supported distributions are Debian (`stable` and `oldstable`) and Ubuntu (currently supported LTS releases). Packages for these releases are built for the `amd64`, `arm64`, and `armhf` architectures and uploaded to http://packages.gobysoft.org

To install release packages on Ubuntu, run:
```
echo "deb http://packages.gobysoft.org/ubuntu/release/ `lsb_release -c -s`/" | sudo tee /etc/apt/sources.list.d/gobysoft_release.list
```

or for Debian:
```
echo "deb http://packages.gobysoft.org/debian/release/ `lsb_release -c -s`/" | sudo tee /etc/apt/sources.list.d/gobysoft_release.list
```

In both cases, then run:

```
sudo apt-key adv --recv-key --keyserver keyserver.ubuntu.com 19478082E2F8D3FE
sudo apt update
# minimal
sudo apt install libgoby3-dev goby3-apps
# full
sudo apt install libgoby3-dev libgoby3-gui-dev goby3-apps goby3-gui goby3-doc goby3-test libgoby3-moos-dev goby3-moos
```

Instead of the release repository, you can use the continuous repository (every commit to the main `3.0` branch build) by adding to your apt sources:

```
deb http://packages.gobysoft.org/[ubuntu|debian]/continuous/ {release-codename}/
```

### Other

Other Linux and UNIX-like operating systems should work, but will require building from source and a bit of dependency searching. For all dependencies to build all parts of Goby, you can start by looking at the `Build-Depends` of the Debian package [control file](https://github.com/GobySoft/goby-debian/blob/3.0/control). Not all of these dependencies are required to build parts of Goby; a minimal build requires these Debian packages, from which you can hopefully find your release's equivalents:

```
# Compiler, CMake
build-essential cmake
# Boost
libboost-dev libboost-system-dev libboost-date-time-dev libboost-program-options-dev libboost-filesystem-dev
# Google Protocol Buffers
libprotobuf-dev libprotoc-dev protobuf-compiler
# PROJ Geodetic Projections
libproj-dev
# DCCL
libdccl3-dev dccl3-compiler
```

Once you have resolved the dependencies, you can build from source using these steps:

   1. [Download a release](https://github.com/GobySoft/goby3/releases) or clone the git repository

    ```git clone https://github.com/GobySoft/goby3.git```

   2. Create an out-of-source build directory and change to it

    ```mkdir goby3/build && cd goby3/build```

   3. Run cmake and build

    ```cmake .. && cmake --build .```

## Building Examples

Please visit <https://github.com/GobySoft/goby3-examples> to learn about the available code examples for Goby.

## Authors

Goby is developed by GobySoft and a number of external contributers (https://github.com/GobySoft/goby3/graphs/contributors). The lead developer is Toby Schneider (https://github.com/tsaubergine).
