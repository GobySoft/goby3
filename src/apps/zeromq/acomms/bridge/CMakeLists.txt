add_executable(goby_bridge bridge.cpp)
target_link_libraries(goby_bridge goby goby_zeromq)
