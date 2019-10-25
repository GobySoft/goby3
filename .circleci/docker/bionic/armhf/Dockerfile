FROM gobysoft/goby3-ubuntu-build-base:18.04.1

ENV DEBIAN_FRONTEND=noninteractive

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libdccl3-dev:armhf \
            libcrypto++-dev:armhf \
            libcrypto++6:armhf \
            libwt-dev:armhf libwtdbo-dev:armhf libwtdbosqlite-dev:armhf libwthttp-dev:armhf \
            libboost-regex-dev:armhf libicu-dev:armhf \
            libhdf5-dev:armhf \
            libgmp3-dev:armhf && \
    rm -rf /var/lib/apt/lists/*

# Bug in Bionic: if this file isn't removed, we get
# dpkg: error processing archive /var/cache/apt/archives/libstdc++-7-dev_7.3.0-27ubuntu1~18.04_armhf.deb (--unpack):
#   trying to overwrite shared '/usr/include/c++/7/ext/random', which is different from other instances of package libstdc++-7-dev:armhf
RUN apt-get update && \
    rm /usr/include/c++/7/ext/random && \
    apt-get install -y libstdc++-7-dev:armhf && \
    rm -rf /var/lib/apt/lists/*
