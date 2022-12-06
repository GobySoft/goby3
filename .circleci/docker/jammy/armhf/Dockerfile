FROM gobysoft/goby3-ubuntu-build-base:22.04.1

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get -y remove libcurl4-openssl-dev libdccl4-dev libdccl4 krb5-multidev

RUN sed -i 's/^\( *\)clang/#\1clang/' debian/control && \
    sed -i 's/^\(.*\)lld/#\1lld/' debian/control && \
    sed -i 's/^\(.*\)libmavlink-dev/#\1libmavlink-dev/' debian/control && \
    sed -i 's/^\(.*\)lmodern/#\1lmodern/' debian/control && \
    cat debian/control

RUN apt-get update && \
    mk-build-deps -a armhf --host-arch armhf --build-arch amd64 -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" && \
    rm -rf /var/lib/apt/lists/*

# Restore control file
RUN git -C debian checkout control
