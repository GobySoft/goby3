#!/bin/bash

if [ -z "${GOBY_CMAKE_FLAGS}" ]; then
    GOBY_CMAKE_FLAGS=
fi

if [ -z "${GOBY_MAKE_FLAGS}" ]; then
    GOBY_MAKE_FLAGS=
fi

set -e -u
echo "Configuring Goby"
echo "cmake .. ${GOBY_CMAKE_FLAGS}"
mkdir -p build
pushd build >& /dev/null
cmake .. ${GOBY_CMAKE_FLAGS}
echo "Building Goby"
echo "make ${GOBY_MAKE_FLAGS} $@"
make ${GOBY_MAKE_FLAGS} $@
popd >& /dev/null
