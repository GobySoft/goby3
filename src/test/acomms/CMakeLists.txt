protobuf_generate_cpp(TEST_PROTO_SRCS TEST_PROTO_HDRS dccl1/test.proto dccl3/test.proto dccl3/header.proto)
add_library(goby_test_proto_messages STATIC ${TEST_PROTO_SRCS} ${TEST_PROTO_HDRS})
# ensure the Goby protos are compiled first
add_dependencies(goby_test_proto_messages goby)
target_compile_options(goby_test_proto_messages PRIVATE -fPIC)

add_subdirectory(queue1)
add_subdirectory(queue2)
add_subdirectory(queue3)
add_subdirectory(queue4)
add_subdirectory(queue5)
add_subdirectory(queue6)

add_subdirectory(amac1)

add_subdirectory(mmdriver1)
add_subdirectory(mmdriver2)

add_subdirectory(route1)
add_subdirectory(udpdriver1)
add_subdirectory(udpdriver2)
add_subdirectory(udpdriver3)

add_subdirectory(iridiumdriver1)
add_subdirectory(iridiumdriver_rockblock1)

add_subdirectory(benthos_atm900_driver1)

add_subdirectory(ipcodecs)

add_subdirectory(udp_multicast_driver1)

add_subdirectory(dynamic_buffer1)

add_subdirectory(popoto_driver1)
