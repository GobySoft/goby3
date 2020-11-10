add_executable(goby_moos_gateway goby_moos_gateway.cpp)

# -rdynamic is required so that the shared libraries can access the interthread static objects correctly 
target_link_libraries(goby_moos_gateway "-rdynamic" goby goby_moos)

generate_middleware_interfaces(goby_moos_gateway)
