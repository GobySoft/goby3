add_test(NAME goby_test_middleware_intervehicle_supersede
  COMMAND ${goby_BIN_DIR}/goby test run ${CMAKE_CURRENT_SOURCE_DIR}/intervehicle_supersede.pb.cfg)
set_tests_properties(goby_test_middleware_intervehicle_supersede PROPERTIES
  TIMEOUT 60 ENVIRONMENT "PATH=${goby_BIN_DIR}:$ENV{PATH}")

add_test(NAME goby_test_middleware_intervehicle_shared_subscription
  COMMAND ${goby_BIN_DIR}/goby test run ${CMAKE_CURRENT_SOURCE_DIR}/intervehicle_shared_subscription.pb.cfg)
set_tests_properties(goby_test_middleware_intervehicle_shared_subscription PROPERTIES
  TIMEOUT 60 ENVIRONMENT "PATH=${goby_BIN_DIR}:$ENV{PATH}")

# intervehicle.pb.cfg loads this library for its DCCL message
protobuf_generate_cpp_dccl(IV_PROTO_SRCS IV_PROTO_HDRS intervehicle_sample.proto)
add_library(goby_test_pubsub_intervehicle_sample SHARED ${IV_PROTO_SRCS} ${IV_PROTO_HDRS})
target_link_libraries(goby_test_pubsub_intervehicle_sample goby)
add_test(NAME goby_test_middleware_pubsub_intervehicle
  COMMAND ${goby_BIN_DIR}/goby test run ${CMAKE_CURRENT_SOURCE_DIR}/intervehicle.pb.cfg)
set_tests_properties(goby_test_middleware_pubsub_intervehicle PROPERTIES
  TIMEOUT 90 ENVIRONMENT
  "PATH=${goby_BIN_DIR}:$ENV{PATH};PSTEST_IV_LIBRARY=$<TARGET_FILE:goby_test_pubsub_intervehicle_sample>")
