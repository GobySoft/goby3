# Tests for the goby Python package: the interface.yml generator, the constants it shares with
# the C++ headers, and the checked-in version. All are pure Python and need no build artifacts.
add_test(NAME goby_test_python_unit
  COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -v
  WORKING_DIRECTORY ${GOBY_PYTHON_SOURCE_DIR})

set_tests_properties(goby_test_python_unit PROPERTIES
  ENVIRONMENT "PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}")

# The Julia generator reads the same interface.yml format, so it must accept and reject the same
# corpus. Skipped when Julia isn't available.
find_program(JULIA_EXECUTABLE julia)
if(JULIA_EXECUTABLE)
  add_test(NAME goby_test_interface_conformance
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/conformance.py
            --julia ${JULIA_EXECUTABLE}
            --goby-jl ${goby_SRC_DIR}/share/Goby.jl
            --corpus ${goby_SRC_DIR}/share/interface/test)
  set_tests_properties(goby_test_interface_conformance PROPERTIES
    ENVIRONMENT "PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}")
else()
  message(STATUS "julia not found: skipping the interface.yml cross-language conformance test")
endif()

# End-to-end: generate the glue from interface.yml, compile it into an extension module, and
# import it. This is the test that proves the generated C++ actually builds.
protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS test.proto)

# the Python protobuf module, generated from the same .proto by the same protoc. Goby copies
# .proto files into the include tree, and the C++ side compiles them from there, so do the same
# here to get matching package paths.
set(PYTHON_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
set(PYTHON_PROTO_OUT ${PYTHON_PROTO_DIR}/goby/test/python/test_pb2.py)
add_custom_command(
  OUTPUT ${PYTHON_PROTO_OUT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PYTHON_PROTO_DIR}
  COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
          --python_out=${PYTHON_PROTO_DIR}
          --proto_path ${goby_INC_DIR} ${ALL_PROTOBUF_INCLUDE_DIRS}
          ${goby_INC_DIR}/goby/test/python/test.proto
  # no __init__.py files: these are implicit namespace packages, reached through the __path__
  # that goby/__init__.py extends. Writing a goby/__init__.py here would shadow the real one.
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test.proto ${PROTO_HDRS}
  COMMENT "Running protoc (python) on test.proto"
  VERBATIM)
add_custom_target(goby_test_python_proto DEPENDS ${PYTHON_PROTO_OUT})

goby_add_python_app(
  TARGET goby_test_python_app
  INTERFACE_YML ${CMAKE_CURRENT_SOURCE_DIR}/interface.yml
  SOURCES ${PROTO_SRCS} ${PROTO_HDRS}
  INCLUDES goby/zeromq/application/single_thread.h
           goby/test/python/test.pb.h
           goby/test/python/groups.h
  PROTO_MODULES goby.test.python.test_pb2
  LINK_LIBRARIES goby goby_zeromq)

add_dependencies(goby_test_python_app goby_test_python_proto goby_python_protos)
target_include_directories(goby_test_python_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# groups.h is included by the generated glue through the goby include tree
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/groups.h ${goby_INC_DIR}/goby/test/python/groups.h COPYONLY)

add_test(NAME goby_test_python_app
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app.py)
set_tests_properties(goby_test_python_app PROPERTIES
  TIMEOUT 60
  ENVIRONMENT "PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}:${goby_test_python_app_PYTHON_DIRECTORY}:${PYTHON_PROTO_DIR}:${GOBY_PYTHON_PROTO_DIR}")
