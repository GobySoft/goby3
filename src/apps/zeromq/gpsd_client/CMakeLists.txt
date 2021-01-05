add_executable(goby_gpsd gpsd_client.cpp)
target_link_libraries(goby_gpsd goby goby_zeromq gps)
