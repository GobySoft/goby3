# creates gobysoft/goby3-ubuntu-build-base:18.04.N
FROM gobysoft/ubuntu-build-base:18.04.1

WORKDIR /root

SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt-get -y install dirmngr && \
    echo "deb http://packages.gobysoft.org/ubuntu/release/ bionic/" >> /etc/apt/sources.list.d/gobysoft_release.list && \
    apt-key adv --recv-key --keyserver keyserver.ubuntu.com 19478082E2F8D3FE

# We need this for Wt for now. Hopefully we'll replace with packages.gobysoft.org later
RUN apt-get update && \
    apt-get -y install software-properties-common && \
    add-apt-repository -y ppa:goby-dev/ppa && \
    apt-get update

# Clone the Debian packaging project and install the dependencies it declares
RUN git clone \
    https://github.com/GobySoft/goby-debian -b 3.0 debian    

RUN apt-get update && \
    mk-build-deps -t "apt-get -y --no-install-recommends" -i "debian/control"

# Comment out troublesome (not fully multi-arch compatible) dependencies
RUN sed -i 's/^\(.*\)clang/#\1clang/' debian/control && \
    sed -i 's/^\(.*\)lmodern/#\1lmodern/' debian/control && \
    sed -i 's/^\(.*\)libwt/#\1libwt/' debian/control && \
    sed -i 's/^\(.*\)libhdf/#\1libhdf/' debian/control && \
    sed -i 's/^\(.*\)libboost-regex/#\1libboost-regex/' debian/control && \
    sed -i 's/^\(.*\)libdccl/#\1libdccl/' debian/control && \
    sed -i 's/^\(.*\)libmoos/#\1libmoos/' debian/control && \
    cat debian/control

RUN apt-get update && \
    ARCHS=(${CROSS_COMPILE_ARCHS}) && \
    for ARCH in "${ARCHS[@]}"; \
    do mk-build-deps -a ${ARCH} --host-arch ${ARCH} --build-arch amd64 -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control"; \
    done && \
    rm -rf /var/lib/apt/lists/*

# Restore control file
RUN git -C debian checkout control


# libdccl3-dev:arm64
# libboost-regex-dev:arm64 libicu-dev:arm64
# libhdf5-dev
# libmoos10-dev:arm64