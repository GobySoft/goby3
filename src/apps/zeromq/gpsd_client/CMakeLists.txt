add_executable(goby_gps gpsd_client.cpp)


generate_middleware_interfaces(goby_gps)

target_link_libraries(goby_gps goby goby_zeromq)
