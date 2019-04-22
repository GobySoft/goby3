FROM gobysoft/goby3-ubuntu-build-base:18.04.1

# Overwrite non-multiarch packages

# TODO Fix:
# dpkg: error processing archive /tmp/apt-dpkg-install-kHcBW2/115-libmoos10_10.0.2-2~ubuntu18.04.1_armhf.deb (--unpack):
# trying to overwrite shared '/usr/share/doc/libmoos10/changelog.Debian.gz', which is different from other instances of package libmoos10:armhf
# dpkg: error processing archive /tmp/apt-dpkg-install-kHcBW2/116-libmoos10-dev_10.0.2-2~ubuntu18.04.1_armhf.deb (--unpack):
# trying to overwrite shared '/usr/share/MOOS/cmake/MOOSConfig-debug.cmake', which is different from other instances of package libmoos10-dev:armhf
RUN apt-get -y remove libmoos10-dev:amd64 libmoos10:amd64

RUN apt-get update && \
    apt-get -y install libdccl3-dev:armhf \
            libwt-dev:armhf libwtdbo-dev:armhf libwtdbosqlite-dev:armhf libwthttp-dev:armhf \
            libdccl3-dev:armhf \
            libboost-regex-dev:armhf libicu-dev:armhf \
            libhdf5-dev:armhf \
            libmoos10-dev:armhf && \
    rm -rf /var/lib/apt/lists/*
