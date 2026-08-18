# Tests for the Julia bindings. The library is built by the normal build, so a generator or
# header change that does not compile fails here rather than in a downstream project.

protobuf_generate_cpp_dccl(PROTO_SRCS PROTO_HDRS test.proto)

# groups.h is included by the generated glue through the goby include tree
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/groups.h ${goby_INC_DIR}/goby/test/julia/groups.h COPYONLY)

goby_add_julia_app(
  TARGET goby_test_julia_app
  INTERFACE_YML ${CMAKE_CURRENT_SOURCE_DIR}/interface.yml
  MAIN ${CMAKE_CURRENT_SOURCE_DIR}/test_app.jl
  SOURCES ${PROTO_SRCS} ${PROTO_HDRS}
  INCLUDES goby/zeromq/application/single_thread.h
           goby/test/julia/test.pb.h
           goby/test/julia/groups.h
  LINK_LIBRARIES goby goby_zeromq
  # nothing here needs threads, but running the launcher is how THREADS gets tested at all
  THREADS 3
  # a test is not an application, so keep the launcher out of the goby bin directory
  LAUNCHER_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(goby_test_julia_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# through the generated launcher, which runs the copy beside the library: the layout and the
# entry point an application actually gets
add_test(NAME goby_test_julia_app
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/goby_test_julia_app ${GOBY_JULIA_DIR})

# Julia compiles its dependencies on first use, which is slow on a cold depot
set_tests_properties(goby_test_julia_app PROPERTIES TIMEOUT 600)
