# creates gobysoft/goby3-debian-build-base:13.N
FROM gobysoft/debian-build-base:13.1

WORKDIR /root

SHELL ["/bin/bash", "-c"]

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get -y install dirmngr wget && \
    export GOBYSOFT_SIGNING_KEY=19478082E2F8D3FE && \
    install -d -m 0755 /etc/apt/keyrings && \
    gpg --keyserver keyserver.ubuntu.com --recv-keys ${GOBYSOFT_SIGNING_KEY} && gpg --export ${GOBYSOFT_SIGNING_KEY} >/etc/apt/keyrings/gobysoft.gpg && \
    echo "deb [signed-by=/etc/apt/keyrings/gobysoft.gpg] http://packages.gobysoft.org/$(. /etc/os-release; echo "$ID")/release/ $(. /etc/os-release; echo "$VERSION_CODENAME")/" >/etc/apt/sources.list.d/gobysoft_release.list

# Clone the Debian packaging project and install the dependencies it declares
RUN git clone \
    https://github.com/GobySoft/goby-debian -b 3.0 debian    

RUN apt-get update && \
    mk-build-deps -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" && \
    rm -rf /var/lib/apt/lists/*
