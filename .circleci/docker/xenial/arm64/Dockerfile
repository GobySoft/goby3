FROM gobysoft/dccl-ubuntu-build-base:16.04.1

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libcrypto++-dev:arm64 && \
    rm -rf /var/lib/apt/lists/*
