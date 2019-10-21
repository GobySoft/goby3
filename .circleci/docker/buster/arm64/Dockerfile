FROM gobysoft/goby3-debian-build-base:10.1

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libdccl3-dev:arm64 \
            libwt-dev:arm64 libwtdbo-dev:arm64 libwtdbosqlite-dev:arm64 libwthttp-dev:arm64 \
            libboost-regex-dev:arm64 libicu-dev:arm64 \
            libhdf5-dev:arm64 \
            libgmp3-dev:arm64 \
            libproj-dev:arm64 \
            libstdc++-8-dev:arm64 && \
    rm -rf /var/lib/apt/lists/*
