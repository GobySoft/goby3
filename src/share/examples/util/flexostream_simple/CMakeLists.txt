add_executable(flexostream_simple flexostream_simple.cpp)
target_link_libraries(flexostream_simple goby ${Boost_LIBRARIES} ${PROTOBUF_LIBRARIES})
install(TARGETS flexostream_simple RUNTIME DESTINATION bin)
