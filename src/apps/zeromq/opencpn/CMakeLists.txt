set(APP goby_opencpn_interface)
add_executable(${APP} opencpn.cpp)
target_link_libraries(${APP} goby goby_zeromq)
generate_middleware_interfaces(${APP})
