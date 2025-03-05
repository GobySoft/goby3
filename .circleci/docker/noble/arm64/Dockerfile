FROM gobysoft/ubuntu-build-base:24.04.1

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get -y install dirmngr && \
    echo "deb http://packages.gobysoft.org/ubuntu/release/ noble/" >> /etc/apt/sources.list.d/gobysoft_release.list && \
    apt-key adv --recv-key --keyserver keyserver.ubuntu.com 19478082E2F8D3FE

# Clone the Debian packaging project and install the dependencies it declares
RUN git clone \
    https://github.com/GobySoft/goby-debian -b 3.0 debian

# This is necessary to avoid a broken package issue - not sure why - try removing in non-noble distros
RUN apt-get update && \
    apt-get -y install libwt4-dev:arm64

RUN apt-get update && \
    mk-build-deps --build-dep -a arm64 --host-arch arm64 --build-arch amd64 -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" && \
    rm -rf /var/lib/apt/lists/*
