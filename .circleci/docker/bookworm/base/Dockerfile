# creates gobysoft/goby3-debian-build-base:12.N
FROM gobysoft/debian-build-base:12.1

WORKDIR /root

SHELL ["/bin/bash", "-c"]

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get -y install dirmngr apt-transport-https && \
    echo "deb http://packages.gobysoft.org/debian/release/ bookworm/" >> /etc/apt/sources.list.d/gobysoft_release.list && \
    apt-key adv --recv-key --keyserver keyserver.ubuntu.com 19478082E2F8D3FE

# Clone the Debian packaging project and install the dependencies it declares
RUN git clone \
    https://github.com/GobySoft/goby-debian -b 3.0 debian    

RUN apt-get update && \
    mk-build-deps -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" && \
    rm -rf /var/lib/apt/lists/*
