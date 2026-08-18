# Tests for the goby Python package: the interface.yml generator, the constants it shares with
# the C++ headers, and the checked-in version. All are pure Python and need no build artifacts.
add_test(NAME goby_test_python_unit
  COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -v
  WORKING_DIRECTORY ${GOBY_PYTHON_SOURCE_DIR})

set_tests_properties(goby_test_python_unit PROPERTIES
  ENVIRONMENT "PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}")

# End-to-end: generate the glue from interface.yml, compile it into an extension module, and
# import it. This is the test that proves the generated C++ actually builds.
protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS test.proto)

# the .proto is compiled to Python from the include tree, as the C++ side compiles it, so that
# the module path matches the protobuf package
goby_add_python_app(
  TARGET goby_test_python_app
  INTERFACE_YML ${CMAKE_CURRENT_SOURCE_DIR}/interface.yml
  SOURCES ${PROTO_SRCS} ${PROTO_HDRS}
  INCLUDES goby/zeromq/application/single_thread.h
           goby/test/python/test.pb.h
           goby/test/python/groups.h
  PYTHON_PROTOS ${goby_INC_DIR}/goby/test/python/test.proto
  PROTO_IMPORT_DIRS ${goby_INC_DIR}
  PROTO_MODULES goby.test.python.test_pb2
  LINK_LIBRARIES goby goby_zeromq)

add_dependencies(goby_test_python_app goby_python_protos)
target_include_directories(goby_test_python_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# groups.h is included by the generated glue through the goby include tree
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/groups.h ${goby_INC_DIR}/goby/test/python/groups.h COPYONLY)

add_test(NAME goby_test_python_app
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app.py)
set_tests_properties(goby_test_python_app PROPERTIES
  TIMEOUT 60
  ENVIRONMENT "PYTHONPATH=${goby_test_python_app_PYTHONPATH}")
