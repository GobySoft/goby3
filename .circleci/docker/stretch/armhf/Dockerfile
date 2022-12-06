FROM gobysoft/goby3-debian-build-base:9.1

ENV DEBIAN_FRONTEND=noninteractive

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libdccl4-dev:armhf \
            libcrypto++-dev:armhf \
            libcrypto++6:armhf \
            libwt-dev:armhf libwtdbo-dev:armhf libwtdbosqlite-dev:armhf libwthttp-dev:armhf \
            libboost-regex-dev:armhf libicu-dev:armhf \
            libhdf5-dev:armhf \
            libgmp3-dev:armhf \
            libproj-dev:armhf \
            libstdc++-6-dev:armhf && \
    rm -rf /var/lib/apt/lists/*
