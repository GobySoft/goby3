add_executable(amac_simple amac_simple.cpp)
target_link_libraries(amac_simple goby ${Boost_LIBRARIES} ${PROTOBUF_LIBRARIES})
install(TARGETS amac_simple RUNTIME DESTINATION bin)
