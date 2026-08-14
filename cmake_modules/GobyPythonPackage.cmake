# GobyPythonPackage.cmake - build and install the goby Python package
#
# Compiles Goby's own .proto files to Python and installs the package.
#
# Sets:
#   GOBY_PYTHON_PROTO_DIR    directory holding the generated Goby Python protobuf modules
#   GOBY_PYTHON_INSTALL_DIR  directory the package is installed into
#
# Adds the target goby_python_protos.

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 REQUIRED)

# protobuf refuses to register the same descriptor file twice in one process, so these are
# compiled once here rather than by each application that embeds AppConfig
set(GOBY_PYTHON_PROTO_DIR ${goby_BUILD_DIR}/python CACHE PATH "Directory holding the generated Goby Python protobuf modules")
file(MAKE_DIRECTORY ${GOBY_PYTHON_PROTO_DIR})

# protobuf is found in the src/ scope, which is not visible here
find_program(GOBY_PROTOC_EXECUTABLE NAMES protoc REQUIRED)
# AppConfig imports dccl/option_extensions.proto; the module compiled from it comes from DCCL
# (python3-dccl5), so this is only needed to resolve the import
find_path(GOBY_DCCL_PROTO_DIR dccl/option_extensions.proto)

file(GLOB_RECURSE GOBY_PYTHON_PROTOBUF_FILES RELATIVE ${goby_SRC_DIR} src/*.proto)
# several test messages reuse the same message names, which protoc rejects in one invocation
list(FILTER GOBY_PYTHON_PROTOBUF_FILES EXCLUDE REGEX "^test/")

set(GOBY_PYTHON_PROTO_INPUTS)
set(GOBY_PYTHON_PROTO_OUTPUTS)
foreach(I ${GOBY_PYTHON_PROTOBUF_FILES})
  string(REGEX REPLACE "\\.proto$" "_pb2.py" PROTO_PY ${I})
  list(APPEND GOBY_PYTHON_PROTO_INPUTS ${goby_INC_DIR}/goby/${I})
  list(APPEND GOBY_PYTHON_PROTO_OUTPUTS ${GOBY_PYTHON_PROTO_DIR}/goby/${PROTO_PY})
endforeach()

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
  # site-packages or dist-packages depending on the distribution; ask rather than guess
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
  # a symlink into share/interface in the source tree, whose relative path does not survive the
  # move to dist-packages
  install(FILES ${goby_SRC_DIR}/share/interface/interface.schema.json
    DESTINATION ${GOBY_PYTHON_INSTALL_DIR}/goby/schema)
  # merged into the same package tree, which goby/__init__.py extends __path__ to allow
  install(DIRECTORY ${GOBY_PYTHON_PROTO_DIR}/
    DESTINATION ${GOBY_PYTHON_INSTALL_DIR}
    FILES_MATCHING PATTERN "*_pb2.py")

  # the [project.scripts] entry point pip and pybuild write for themselves. Kept out of
  # goby_BIN_DIR, which is installed whatever GOBY_INSTALL_PYTHON_RUNTIME says, and out of
  # GOBY_PYTHON_PROTO_DIR, whose tree is mirrored into the install.
  file(GENERATE OUTPUT ${goby_BUILD_DIR}/python_scripts/goby_gen_cpp
    CONTENT "#!/usr/bin/env python3
import sys

from goby.gen import main

if __name__ == \"__main__\":
    sys.exit(main())
")
  install(PROGRAMS ${goby_BUILD_DIR}/python_scripts/goby_gen_cpp DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  message(STATUS "Not installing the goby Python package (GOBY_INSTALL_PYTHON_RUNTIME=OFF)")
endif()
