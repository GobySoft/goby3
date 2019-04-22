FROM gobysoft/goby3-ubuntu-build-base:18.04.1

# Overwrite non-multiarch packages

# TODO Fix:
# dpkg: error processing archive /tmp/apt-dpkg-install-kHcBW2/115-libmoos10_10.0.2-2~ubuntu18.04.1_arm64.deb (--unpack):
# trying to overwrite shared '/usr/share/doc/libmoos10/changelog.Debian.gz', which is different from other instances of package libmoos10:arm64
# dpkg: error processing archive /tmp/apt-dpkg-install-kHcBW2/116-libmoos10-dev_10.0.2-2~ubuntu18.04.1_arm64.deb (--unpack):
# trying to overwrite shared '/usr/share/MOOS/cmake/MOOSConfig-debug.cmake', which is different from other instances of package libmoos10-dev:arm64
RUN apt-get -y remove libmoos10-dev:amd64 libmoos10:amd64

RUN apt-get update && \
    apt-get -y install libdccl3-dev:arm64 \
            libwt-dev:arm64 libwtdbo-dev:arm64 libwtdbosqlite-dev:arm64 libwthttp-dev:arm64 \
            libdccl3-dev:arm64 \
            libboost-regex-dev:arm64 libicu-dev:arm64 \
            libhdf5-dev:arm64 \
            libmoos10-dev:arm64 && \
    rm -rf /var/lib/apt/lists/*
