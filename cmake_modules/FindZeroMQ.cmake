find_path(ZeroMQ_INCLUDE_DIR zmq.hpp)

find_library(ZeroMQ_LIBRARY NAMES zmqpp zmq
  DOC "The ZeroMQ message passing library" PATHS /usr/lib)

mark_as_advanced(ZeroMQ_INCLUDE_DIR
  ZeroMQ_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ZeroMQ DEFAULT_MSG
  ZeroMQ_LIBRARY ZeroMQ_INCLUDE_DIR)

set(ZeroMQ_FOUND ${ZEROMQ_FOUND}) 
if(ZeroMQ_FOUND)
  set(ZeroMQ_INCLUDE_DIRS ${ZeroMQ_INCLUDE_DIR})
  set(ZeroMQ_LIBRARIES    ${ZeroMQ_LIBRARY})
endif()
