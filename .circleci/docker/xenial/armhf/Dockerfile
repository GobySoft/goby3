FROM gobysoft/goby3-ubuntu-build-base:16.04.1

ENV DEBIAN_FRONTEND=noninteractive

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libdccl3-dev:armhf \
            libcrypto++-dev:armhf \
            libcrypto++9v5:armhf \
            libwt-dev:armhf libwtdbo-dev:armhf libwtdbosqlite-dev:armhf libwthttp-dev:armhf \
            libboost-regex-dev:armhf libicu-dev:armhf \
            libhdf5-dev:armhf \
            libgmp3-dev:armhf \
            libproj-dev:armhf \
            libstdc++-5-dev:armhf && \
    rm -rf /var/lib/apt/lists/*
