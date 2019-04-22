# creates gobysoft/dccl-debian-build-base:9.N
FROM gobysoft/debian-build-base:9.1

WORKDIR /root

SHELL ["/bin/bash", "-c"]

# Clone the Debian packaging project and install the dependencies it declares
RUN git clone \
    https://github.com/GobySoft/dccl-debian -b 3.0 debian    

RUN ARCHS=(${CROSS_COMPILE_ARCHS}) && \
    apt-get update && \
    mk-build-deps -t "apt-get -y --no-install-recommends" -i "debian/control" && \    
    for ARCH in "${ARCHS[@]}"; \
    do mk-build-deps -a ${ARCH} --host-arch ${ARCH} --build-arch amd64 -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control"; \
    done && \
    rm -rf /var/lib/apt/lists/*

# Overwrite non-multiarch packages
RUN apt-get update && \
    apt-get -y install libcrypto++-dev && \
    rm -rf /var/lib/apt/lists/*
