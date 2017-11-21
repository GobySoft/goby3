
file(GLOB PROTOS protobuf/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

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
   frontseat/frontseat.cpp
   middleware/moos_plugin_translator.cpp
)

if(enable_v1_transitional)
  file(GLOB TRANSITIONAL_SRC transitional/*.cpp)
endif()

if(enable_zeromq)
  set(ZEROMQ_SRC moos_node.cpp)
endif()

add_library(goby_moos ${SRC} ${ZEROMQ_SRC} ${TRANSITIONAL_SRC} ${PROTO_SRCS} ${PROTO_HDRS})
goby_install_lib(goby_moos)

target_link_libraries(goby_moos
  goby_util
  goby_common 
  goby_acomms
  goby_middleware
  proj
  ${MOOS_LIBRARIES} ${Boost_LIBRARIES} ${PROTOBUF_LIBRARY}
)

if(enable_v1_transitional)
  target_link_libraries(goby_moos ${Xerces_LIBRARIES})
endif()

set_target_properties(goby_moos PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")


if(enable_wt AND enable_zeromq)
  add_library(liaison_plugins_goby_moos
    moos_liaison_load.cpp
    liaison_scope.cpp 
    liaison_commander.cpp
    liaison_geodesy.cpp
    liaison_acomms.cpp)

  target_link_libraries(liaison_plugins_goby_moos ${Wt_LIBRARIES} goby_moos ${Boost_LIBRARIES})
  goby_install_lib(liaison_plugins_goby_moos)
endif()

add_subdirectory(frontseat)
