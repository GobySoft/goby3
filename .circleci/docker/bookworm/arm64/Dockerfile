FROM gobysoft/goby3-debian-build-base:12.1

ENV DEBIAN_FRONTEND=noninteractive

# https://bugs.launchpad.net/ubuntu/+source/freerdp2/+bug/2017998
# affects libzmq3-dev

# libcurl4-openssl-dev breaks libhdf5-dev

RUN apt-get -y remove libcurl4-openssl-dev libdccl4-dev libdccl4 libzmq3-dev goby3-build-deps libproj-dev libhdf5-dev

RUN apt-get update && \
    mk-build-deps --build-dep -a arm64 --host-arch arm64 --build-arch amd64 -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" && \
    rm -rf /var/lib/apt/lists/*
