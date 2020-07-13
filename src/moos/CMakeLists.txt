set(PROTOS 
  protobuf/bluefin_driver.proto
  protobuf/goby_moos_app.proto
  protobuf/modem_id_lookup.proto
  protobuf/pAcommsHandler_config.proto
  protobuf/iFrontSeat_config.proto
  protobuf/translator.proto
  protobuf/ufield_sim_driver.proto
  protobuf/node_status.proto
  protobuf/desired_course.proto
  protobuf/moos_helm_frontseat_interface_config.proto
  )

set(SRC
   goby_moos_app.cpp
   modem_id_convert.cpp
   moos_translator.cpp
   moos_protobuf_helpers.cpp
   moos_ufield_sim_driver.cpp
   moos_bluefin_driver.cpp
   transitional/message_val.cpp
   transitional/message_algorithms.cpp
   moos_geodesy.cpp
   frontseat/convert.cpp
   )


if(build_zeromq)
  set(PROTOS ${PROTOS} 
    protobuf/liaison_config.proto
    protobuf/moos_gateway_config.proto
    )
  set(SRC ${SRC}
    middleware/moos_plugin_translator.cpp
    )
endif()

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  ${PROTOS}
  )

add_library(goby_moos ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})
goby_install_lib(goby_moos moos)

target_link_libraries(goby_moos
  goby
  proj
  ${MOOS_LIBRARIES} ${Boost_LIBRARIES} ${PROTOBUF_LIBRARY}
)

if(build_zeromq)
  target_link_libraries(goby_moos goby_zeromq)
endif()

set_target_properties(goby_moos PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

if(build_zeromq)
  add_subdirectory(middleware)
endif()
