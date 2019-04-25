FROM gobysoft/goby3-ubuntu-build-base:18.04.1

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libdccl3-dev:arm64 \
            libwt-dev:arm64 libwtdbo-dev:arm64 libwtdbosqlite-dev:arm64 libwthttp-dev:arm64 \
            libboost-regex-dev:arm64 libicu-dev:arm64 \
            libhdf5-dev:arm64 \
            libgmp3-dev:arm64 && \           
    rm -rf /var/lib/apt/lists/*

# Bug in Bionic: if this file isn't removed, we get
# dpkg: error processing archive /var/cache/apt/archives/libstdc++-7-dev_7.3.0-27ubuntu1~18.04_arm64.deb (--unpack):
#   trying to overwrite shared '/usr/include/c++/7/ext/random', which is different from other instances of package libstdc++-7-dev:arm64
RUN apt-get update && \
    rm /usr/include/c++/7/ext/random && \
    apt-get install -y libstdc++-7-dev:arm64 && \
    rm -rf /var/lib/apt/lists/*
