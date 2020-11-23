add_executable(gobyd gobyd.cpp)
target_link_libraries(gobyd goby goby_zeromq)

generate_middleware_interfaces(gobyd)
