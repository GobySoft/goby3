# GobyPythonPackage.cmake - build and install the goby Python package
#
# Included by the top level CMakeLists.txt when build_python is ON. Compiles Goby's own .proto
# files to Python, works out where the package should be installed, and installs it. The package
# itself is pure Python, so all that is needed is to put it somewhere importable.
#
# Sets:
#   GOBY_PYTHON_PROTO_DIR    directory holding the generated Goby Python protobuf modules
#   GOBY_PYTHON_INSTALL_DIR  directory the package is installed into
#
# Adds the target goby_python_protos.

# GobyPython.cmake has already looked for these; fail loudly if build_python was forced ON
# without them
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 REQUIRED)

# Goby's own .proto files have to be compiled to Python once and shipped with the goby package:
# every application configuration embeds goby.middleware.protobuf.AppConfig, and protobuf
# refuses to register the same descriptor file twice in one process, so applications cannot
# each generate their own copy.
set(GOBY_PYTHON_PROTO_DIR ${goby_BUILD_DIR}/python CACHE PATH "Directory holding the generated Goby Python protobuf modules")
file(MAKE_DIRECTORY ${GOBY_PYTHON_PROTO_DIR})

# protobuf itself is found in the src/ scope, so find what we need here rather than reaching
# into a child scope
find_program(GOBY_PROTOC_EXECUTABLE NAMES protoc REQUIRED)
find_path(GOBY_DCCL_PROTO_DIR dccl/option_extensions.proto)

file(GLOB_RECURSE GOBY_PYTHON_PROTOBUF_FILES RELATIVE ${goby_SRC_DIR} src/*.proto)
# Goby's own test messages are not part of the public interface, and several of them reuse the
# same message names, which protoc rejects when they are compiled in one invocation
list(FILTER GOBY_PYTHON_PROTOBUF_FILES EXCLUDE REGEX "^test/")

set(GOBY_PYTHON_PROTO_INPUTS)
set(GOBY_PYTHON_PROTO_OUTPUTS)
foreach(I ${GOBY_PYTHON_PROTOBUF_FILES})
  string(REGEX REPLACE "\\.proto$" "_pb2.py" PROTO_PY ${I})
  list(APPEND GOBY_PYTHON_PROTO_INPUTS ${goby_INC_DIR}/goby/${I})
  list(APPEND GOBY_PYTHON_PROTO_OUTPUTS ${GOBY_PYTHON_PROTO_DIR}/goby/${PROTO_PY})
endforeach()

# AppConfig imports dccl/option_extensions.proto, and DCCL ships no Python bindings of its own
if(GOBY_DCCL_PROTO_DIR)
  list(APPEND GOBY_PYTHON_PROTO_INPUTS ${GOBY_DCCL_PROTO_DIR}/dccl/option_extensions.proto)
  list(APPEND GOBY_PYTHON_PROTO_OUTPUTS ${GOBY_PYTHON_PROTO_DIR}/dccl/option_extensions_pb2.py)
endif()

add_custom_command(
  OUTPUT ${GOBY_PYTHON_PROTO_OUTPUTS}
  COMMAND ${GOBY_PROTOC_EXECUTABLE}
          --python_out=${GOBY_PYTHON_PROTO_DIR}
          -I${goby_INC_DIR} -I${GOBY_DCCL_PROTO_DIR}
          ${GOBY_PYTHON_PROTO_INPUTS}
  DEPENDS ${GOBY_PYTHON_PROTO_INPUTS}
  COMMENT "Running protoc (python) on the Goby protobuf files"
  VERBATIM)
add_custom_target(goby_python_protos ALL DEPENDS ${GOBY_PYTHON_PROTO_OUTPUTS})

if(NOT GOBY_PYTHON_INSTALL_DIR)
  # Debian and derivatives use dist-packages rather than site-packages; ask the interpreter
  # rather than guessing, and let the caller override
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
      "import sysconfig, os; print(os.path.relpath(sysconfig.get_path('purelib'), sysconfig.get_path('data')))"
    OUTPUT_VARIABLE GOBY_PYTHON_INSTALL_RELATIVE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(GOBY_PYTHON_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/${GOBY_PYTHON_INSTALL_RELATIVE_DIR}
      CACHE PATH "Directory to install the goby Python package into")
endif()

if(GOBY_INSTALL_PYTHON_RUNTIME)
  message(STATUS "Installing the goby Python package to ${GOBY_PYTHON_INSTALL_DIR}")
  install(DIRECTORY ${GOBY_PYTHON_SOURCE_DIR}/goby
    DESTINATION ${GOBY_PYTHON_INSTALL_DIR}
    PATTERN "__pycache__" EXCLUDE
    PATTERN "interface.schema.json" EXCLUDE)
  # In the source tree that file is a symlink to share/interface/interface.schema.json, which is
  # the copy the Julia bindings use. Installing the symlink leaves it dangling, since the same
  # relative path does not lead anywhere from dist-packages, so install what it points at: the
  # package is meant to carry its own schema.
  install(FILES ${goby_SRC_DIR}/share/interface/interface.schema.json
    DESTINATION ${GOBY_PYTHON_INSTALL_DIR}/goby/schema)
  # the generated protobuf modules merge into the same package tree; goby/__init__.py extends
  # __path__ so that they can also be found from a separate directory (an uninstalled build)
  install(DIRECTORY ${GOBY_PYTHON_PROTO_DIR}/
    DESTINATION ${GOBY_PYTHON_INSTALL_DIR}
    FILES_MATCHING PATTERN "*_pb2.py")

  # the [project.scripts] entry point from pyproject.toml, which pip or pybuild would write but
  # a plain install(DIRECTORY) does not. GobyPython.cmake looks for it on PATH, so without it a
  # project building a Goby Python application against an installed Goby has no generator.
  # not written into goby_BIN_DIR: that whole directory is installed unconditionally, and this
  # script must not be when pybuild is the one installing the package
  file(GENERATE OUTPUT ${GOBY_PYTHON_PROTO_DIR}/scripts/goby_gen_cpp
    CONTENT "#!/usr/bin/env python3
# Entry point for the Goby interface.yml generator; see goby/gen.py in the goby Python package.
import sys

from goby.gen import main

if __name__ == \"__main__\":
    sys.exit(main())
")
  install(PROGRAMS ${GOBY_PYTHON_PROTO_DIR}/scripts/goby_gen_cpp DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  message(STATUS "Not installing the goby Python package (GOBY_INSTALL_PYTHON_RUNTIME=OFF)")
endif()
