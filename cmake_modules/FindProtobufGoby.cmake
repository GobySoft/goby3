# Locate and configure the Google Protocol Buffers library.
# Defers to the new CMake config shipped with Protobuf

# Adds the following functions:
# PROTOBUF_GENERATE_CPP_DCCL
# PROTOBUF_INCLUDE_DIRS

function(PROTOBUF_INCLUDE_DIRS)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_INCLUDE_DIRS() called without any directories")
    return()
  endif()  

  foreach(DIR ${ARGN})
    set(ALL_PROTOBUF_INCLUDE_DIRS "${ALL_PROTOBUF_INCLUDE_DIRS};-I${DIR}" PARENT_SCOPE)
  endforeach()
endfunction()


function(PROTOBUF_GENERATE_CPP_DCCL SRCS HDRS)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP_DCCL() called without any proto files")
    return()
  endif(NOT ARGN)

#  file(MAKE_DIRECTORY ${goby_BUILD_DIR}/proto) 

  set(${SRCS})
  set(${HDRS})
  foreach(FIL ${ARGN})
    # /home/toby/goby/src/core/proto/foo.proto
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    # foo
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    # core/proto/foo.proto
    file(RELATIVE_PATH REL_FIL ${goby_SRC_DIR} ${ABS_FIL})
    # /home/toby/goby/include/goby/core/proto/foo.proto
    set(ABS_BUILT_FIL "${goby_INC_DIR}/goby/${REL_FIL}")
    # /home/toby/goby/include/goby/core/proto
    get_filename_component(FIL_PATH ${ABS_BUILT_FIL} PATH)

    # message(STATUS ${ABS_FIL})
    # message(STATUS ${FIL_WE})
    # message(STATUS ${REL_FIL})
    # message(STATUS ${FIL_PATH})

#    include_directories(${FIL_PATH})

    list(APPEND ${SRCS} "${FIL_PATH}/${FIL_WE}.pb.cc")
    list(APPEND ${HDRS} "${FIL_PATH}/${FIL_WE}.pb.h")

    add_custom_command(
      OUTPUT "${FIL_PATH}/${FIL_WE}.pb.cc"
             "${FIL_PATH}/${FIL_WE}.pb.h"
      COMMAND  ${PROTOBUF_PROTOC_EXECUTABLE}
      ARGS --cpp_out ${goby_INC_DIR} --proto_path ${goby_INC_DIR} ${goby_INC_DIR}/goby/${REL_FIL} ${ALL_PROTOBUF_INCLUDE_DIRS} -I ${goby_INC_DIR} --dccl_out ${goby_INC_DIR}
      # add guards for Clang static analyzer (scan-build)
      COMMAND /bin/bash
      ARGS -c "FILE=${FIL_PATH}/${FIL_WE}.pb.cc && TMPFILE=\${FILE}.\${RANDOM} && cat <(echo '#ifndef __clang_analyzer__') \${FILE} <(echo -e '\\n#endif // __clang_analyzer__') > \${TMPFILE} && mv \${TMPFILE} \${FILE}"
      DEPENDS ${ABS_FIL}
      COMMENT "Running C++ protocol buffer compiler on ${FIL}"
      VERBATIM )
  endforeach()

  # copy headers for generated headers
  file(GLOB_RECURSE INCLUDE_FILES RELATIVE ${goby_BUILD_DIR}/proto build/proto/*.h)
  foreach(I ${INCLUDE_FILES})
    configure_file(${goby_BUILD_DIR}/proto/${I} ${goby_INC_DIR}/${I} COPYONLY)
  endforeach()
  
  set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()

find_path(PROTOBUF_INCLUDE_DIR google/protobuf/service.h)

# so that we can use Google's included descriptor.proto
list(APPEND ALL_PROTOBUF_INCLUDE_DIRS "-I${PROTOBUF_INCLUDE_DIR}")

# prefer CMake included with Protobuf
find_package(protobuf QUIET CONFIG)
# if that fails, use the CMake shipped module
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED MODULE)
  set(protobuf_VERSION ${Protobuf_VERSION})
endif()

if(Protobuf_FOUND)
  set(ProtobufGoby_FOUND True)
endif()
