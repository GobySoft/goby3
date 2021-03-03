#!/bin/bash

if [ -z "${GOBY_CMAKE_FLAGS}" ]; then
    GOBY_CMAKE_FLAGS=
fi

if [ -z "${GOBY_MAKE_FLAGS}" ]; then
    GOBY_MAKE_FLAGS=
fi

set -e -u
echo "Configuring Goby"
mkdir -p build
pushd build >& /dev/null
(set -x; cmake .. ${GOBY_CMAKE_FLAGS})
echo "Building Goby"
(set -x; cmake --build . -- -j`nproc` ${GOBY_MAKE_FLAGS} $@ )
popd >& /dev/null
