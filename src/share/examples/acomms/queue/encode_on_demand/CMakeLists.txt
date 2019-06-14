protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS on_demand.proto)

add_executable(queue_encode_on_demand encode_on_demand.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(queue_encode_on_demand goby ${Boost_LIBRARIES} ${PROTOBUF_LIBRARIES})
