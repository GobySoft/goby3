file(GLOB PROTOS protobuf/*.proto)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

set(SRC
  ip_codecs.cpp
  dccl/dccl.cpp
  queue/queue.cpp
  queue/queue_manager.cpp
  amac/mac_manager.cpp
  modemdriver/abc_driver.cpp
  modemdriver/driver_base.cpp
  modemdriver/mm_driver.cpp
  modemdriver/udp_driver.cpp
  modemdriver/rudics_packet.cpp
  modemdriver/iridium_driver.cpp
  modemdriver/iridium_driver_fsm.cpp
  modemdriver/iridium_shore_driver.cpp
  modemdriver/benthos_atm900_driver.cpp
  modemdriver/benthos_atm900_driver_fsm.cpp
  route/route.cpp
  ${PROTO_SRCS} ${PROTO_HDRS}
)

add_library(goby_acomms ${SRC})

target_link_libraries(goby_acomms goby_util goby_common dccl dccl_arithmetic ${Cryptopp_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_DL_LIBS}  ${GMP_LIBRARIES})

set_target_properties(goby_acomms PROPERTIES VERSION "${GOBY_VERSION}" SOVERSION "${GOBY_SOVERSION}")

goby_install_lib(goby_acomms core)
