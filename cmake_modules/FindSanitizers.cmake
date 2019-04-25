option(SANITIZE_ADDRESS "Enable AddressSanitizer." Off)
option(SANITIZE_THREAD "Enable ThreadSanitizer." Off)
option(SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer." Off)

if(SANITIZE_ADDRESS AND SANITIZE_THREAD)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer are mutually "
    "exclusive.")
endif()

set(SANITIZE_ADDRESS_FLAGS "-g -fsanitize=address -fno-omit-frame-pointer")
set(SANITIZE_THREAD_FLAGS "-g -fsanitize=thread")
set(SANITIZE_UNDEFINED_FLAGS "-g -fsanitize=undefined -fno-omit-frame-pointer")

if(SANITIZE_ADDRESS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZE_ADDRESS_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZE_ADDRESS_FLAGS}")
endif()

if(SANITIZE_THREAD)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZE_THREAD_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZE_THREAD_FLAGS}")
endif()

if(SANITIZE_UNDEFINED)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZE_UNDEFINED_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZE_UNDEFINED_FLAGS}")
endif()

