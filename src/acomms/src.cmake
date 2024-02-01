protobuf_generate_cpp(ACOMMS_PROTO_SRCS ACOMMS_PROTO_HDRS
  acomms/protobuf/abc_driver.proto
  acomms/protobuf/amac_config.proto
  acomms/protobuf/amac.proto
  acomms/protobuf/benthos_atm900.proto
  acomms/protobuf/dccl.proto
  acomms/protobuf/driver_base.proto
  acomms/protobuf/file_transfer.proto
  acomms/protobuf/iridium_driver.proto
  acomms/protobuf/iridium_sbd_directip.proto
  acomms/protobuf/iridium_shore_driver.proto
  acomms/protobuf/manipulator.proto
  acomms/protobuf/mm_driver.proto
  acomms/protobuf/modem_driver_status.proto
  acomms/protobuf/modem_message.proto
  acomms/protobuf/mosh_packet.proto
  acomms/protobuf/network_ack.proto
  acomms/protobuf/network_header.proto
  acomms/protobuf/queue.proto
  acomms/protobuf/route.proto
  acomms/protobuf/rudics_shore.proto
  acomms/protobuf/store_server.proto
  acomms/protobuf/time_update.proto
  acomms/protobuf/udp_driver.proto
  acomms/protobuf/udp_multicast_driver.proto
  acomms/protobuf/buffer.proto
  acomms/protobuf/popoto_driver.proto
  acomms/protobuf/janus_driver.proto
  )

set(ACOMMS_SRC
  acomms/ip_codecs.cpp
  acomms/dccl/dccl.cpp
  acomms/queue/queue.cpp
  acomms/queue/queue_manager.cpp
  acomms/amac/mac_manager.cpp
  acomms/modemdriver/abc_driver.cpp
  acomms/modemdriver/driver_base.cpp
  acomms/modemdriver/mm_driver.cpp
  acomms/modemdriver/udp_driver.cpp
  acomms/modemdriver/udp_multicast_driver.cpp
  acomms/modemdriver/rudics_packet.cpp
  acomms/modemdriver/iridium_driver.cpp
  acomms/modemdriver/iridium_driver_fsm.cpp
  acomms/modemdriver/iridium_shore_driver.cpp
  acomms/modemdriver/benthos_atm900_driver.cpp
  acomms/modemdriver/benthos_atm900_driver_fsm.cpp
  acomms/route/route.cpp
  acomms/modemdriver/popoto_driver.cpp
  acomms/modemdriver/janus_driver.cpp
  ${ACOMMS_PROTO_SRCS} ${ACOMMS_PROTO_HDRS}
  )
