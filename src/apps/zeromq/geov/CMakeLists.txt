set(APP goby_geov_interface)
add_executable(${APP} geov.cpp)
target_link_libraries(${APP} goby goby_zeromq ${MYSQL_LIBRARY})
generate_middleware_interfaces(${APP})
