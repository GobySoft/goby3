# Build targets:
#   base (amd64): docker build -f .circleci/docker/resolute/Dockerfile --build-arg TARGET_ARCH=amd64 -t gobysoft/goby3-ubuntu-build-base:22.04.N .
#   arm64:        docker build -f .circleci/docker/resolute/Dockerfile --build-arg TARGET_ARCH=arm64 -t gobysoft/goby3-ubuntu-build-arm64:22.04.N .
#   armhf:        docker build -f .circleci/docker/resolute/Dockerfile --build-arg TARGET_ARCH=armhf -t gobysoft/goby3-ubuntu-build-armhf:22.04.N .
FROM gobysoft/ubuntu-build-base:22.04.1

ARG TARGET_ARCH=amd64

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
    case "${TARGET_ARCH}" in \
      amd64) \
        mk-build-deps -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" ;; \
      arm64|armhf) \
        mk-build-deps --build-dep -a "${TARGET_ARCH}" --host-arch "${TARGET_ARCH}" --build-arch amd64 -t "apt-get -y --no-install-recommends -o Debug::pkgProblemResolver=yes" -i "debian/control" ;; \
      *) \
        echo "Unsupported TARGET_ARCH: ${TARGET_ARCH}. Expected one of: amd64, arm64, armhf." >&2; \
        exit 1 ;; \
    esac && \
    rm -rf /var/lib/apt/lists/*
