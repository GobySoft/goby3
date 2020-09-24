set(SRC frontseat_interface.cpp)

if(build_moos)
  set(SRC
    moos_helm_interface.cpp
    ${SRC})
else()
  set(SRC
    empty_helm_interface.cpp
    ${SRC})
endif()

add_executable(goby_frontseat_interface ${SRC})
if(build_moos)
  target_link_libraries(goby_frontseat_interface goby goby_zeromq goby_moos goby_frontseat_moos_gateway_plugin)
else()
  target_link_libraries(goby_frontseat_interface goby goby_zeromq)
endif()

generate_middleware_interfaces(goby_frontseat_interface)
