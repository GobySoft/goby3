
file(GLOB PROTOS protobuf/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC
   modem_id_convert.cpp
   moos_translator.cpp
   moos_protobuf_helpers.cpp
   moos_ufield_sim_driver.cpp
   moos_bluefin_driver.cpp
   transitional/message_val.cpp
   transitional/message_algorithms.cpp
   moos_geodesy.cpp
   frontseat/frontseat.cpp
   middleware/moos_plugin_translator.cpp
)

add_library(goby_moos ${SRC} ${PROTO_SRCS} ${PROTO_HDRS})
goby_install_lib(goby_moos moos)

target_link_libraries(goby_moos
  goby
  proj
  ${MOOS_LIBRARIES} ${Boost_LIBRARIES} ${PROTOBUF_LIBRARY}
)

set_target_properties(goby_moos PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

add_subdirectory(frontseat)
