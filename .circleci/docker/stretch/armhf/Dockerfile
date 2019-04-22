FROM gobysoft/dccl-debian-build-base:9.1

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libcrypto++-dev:armhf && \
    rm -rf /var/lib/apt/lists/*
