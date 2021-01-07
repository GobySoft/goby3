add_executable(goby_gps gpsd_client.cpp)
target_link_libraries(goby_gps goby goby_zeromq gps)
