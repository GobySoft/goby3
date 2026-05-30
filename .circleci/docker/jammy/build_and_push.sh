#!/bin/bash

set -euo pipefail

for arch in amd64 arm64 armhf; do
    archname=$arch
    if [[ "$arch" = "amd64" ]]; then
        archname="base"
    fi
    tag="gobysoft/goby3-ubuntu-build-$archname:22.04.1"
    docker build --no-cache --build-arg TARGET_ARCH=$arch -t $tag .
    docker push $tag
done
