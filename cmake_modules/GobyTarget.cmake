# GobyTarget.cmake - build Goby applications and libraries
#
# Included by goby-config.cmake, so find_package(GOBY) is enough to get:
#
#   goby_add_app(TARGET <name>
#     SOURCES <files>...
#     [PROTOS <proto_files>...]
#     [LINK_LIBRARIES <libs>...]
#     [PROTO_IMPORT_DIRS <dirs>...]
#     [PROTOC_OUT_DIR <dir>])
#
#   goby_add_library(TARGET <name>
#     [SOURCES <files>...]
#     [PROTOS <proto_files>...]
#     [LINK_LIBRARIES <libs>...]
#     [PROTO_IMPORT_DIRS <dirs>...]
#     [PROTOC_OUT_DIR <dir>]
#     [STATIC] [MODULE])
#
# PROTOS are compiled with protoc --cpp_out and --dccl_out and built into the target, so a
# project declares its messages beside the code that uses them rather than generating them
# separately. Goby's own .proto directory and DCCL's are always searched, after any
# PROTO_IMPORT_DIRS and the project-wide GOBY_PROTOBUF_IMPORT_DIRS.
#
# goby_add_app links goby and installs to bin; goby_add_library is SHARED unless STATIC or
# MODULE is given, and links nothing on its own.

if(GOBY_TARGET_INCLUDED)
  return()
endif()
set(GOBY_TARGET_INCLUDED TRUE)

# AppConfig imports dccl/option_extensions.proto, so any application configuration needs it
find_path(GOBY_DCCL_PROTO_DIR dccl/option_extensions.proto)

# Resolved when a target is declared rather than here: goby_INC_DIR and GOBY_INCLUDE_DIR are
# both set later than this file is read.
function(_GOBY_TARGET_PROTO_DIR out_var)
  if(GOBY_PROTO_DIR)
    set(${out_var} "${GOBY_PROTO_DIR}" PARENT_SCOPE)
  elseif(goby_INC_DIR)
    set(${out_var} "${goby_INC_DIR}" PARENT_SCOPE)
  else()
    set(${out_var} "${GOBY_INCLUDE_DIR}" PARENT_SCOPE)
  endif()
endfunction()

# Compiles .proto files with --cpp_out and --dccl_out, setting out_var to the generated sources
function(_GOBY_TARGET_GENERATE_PROTOS out_var protoc_out_dir protos import_dirs)
  _goby_target_proto_dir(_goby_proto_dir)

  set(_import_flags)
  set(_seen_dirs)
  # the caller's directories first: they decide which file a relative import resolves to
  foreach(_dir
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}
      ${protoc_out_dir}
      ${import_dirs}
      ${GOBY_PROTOBUF_IMPORT_DIRS}
      ${_goby_proto_dir}
      ${GOBY_DCCL_PROTO_DIR})
    # existence is deliberately not checked: these are commonly build tree directories that
    # nothing has written to yet when the target is declared
    if(NOT "${_dir}" STREQUAL "" AND NOT "${_dir}" MATCHES "NOTFOUND$")
      get_filename_component(_abs_dir "${_dir}" ABSOLUTE)
      if(NOT "${_abs_dir}" IN_LIST _seen_dirs)
        list(APPEND _seen_dirs "${_abs_dir}")
        list(APPEND _import_flags -I "${_abs_dir}")
      endif()
    endif()
  endforeach()

  if(TARGET protobuf::protoc)
    set(_protoc protobuf::protoc)
  else()
    find_program(GOBY_PROTOC_EXECUTABLE NAMES protoc)
    if(NOT GOBY_PROTOC_EXECUTABLE)
      message(FATAL_ERROR "goby_add_app/goby_add_library: PROTOS given but protoc was not found")
    endif()
    set(_protoc "${GOBY_PROTOC_EXECUTABLE}")
  endif()

  set(_generated)
  foreach(_proto ${protos})
    get_filename_component(_abs_proto "${_proto}" ABSOLUTE)
    get_filename_component(_proto_we "${_abs_proto}" NAME_WE)

    set(_pb_h "${protoc_out_dir}/${_proto_we}.pb.h")
    set(_pb_cc "${protoc_out_dir}/${_proto_we}.pb.cc")

    # --dccl_out comes last so that the DCCL plugin inserts into what --cpp_out wrote
    add_custom_command(
      OUTPUT "${_pb_h}" "${_pb_cc}"
      COMMAND ${_protoc}
      ARGS --cpp_out "${protoc_out_dir}"
           "${_abs_proto}"
           ${_import_flags}
           --dccl_out "${protoc_out_dir}"
      DEPENDS "${_abs_proto}"
      COMMENT "Running dccl protocol buffer compiler on ${_proto}"
      VERBATIM)

    set_source_files_properties("${_pb_h}" "${_pb_cc}" PROPERTIES GENERATED TRUE)
    list(APPEND _generated "${_pb_h}" "${_pb_cc}")
  endforeach()

  set(${out_var} "${_generated}" PARENT_SCOPE)
endfunction()

function(_GOBY_TARGET_PROTOC_OUT_DIR out_var target given)
  if(given)
    set(_dir "${given}")
  else()
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}")
  endif()
  file(MAKE_DIRECTORY "${_dir}")
  set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

function(GOBY_ADD_APP)
  cmake_parse_arguments(GAA "" "TARGET;PROTOC_OUT_DIR"
    "SOURCES;PROTOS;LINK_LIBRARIES;PROTO_IMPORT_DIRS" ${ARGN})

  if(NOT GAA_TARGET)
    message(FATAL_ERROR "goby_add_app: TARGET is required")
  endif()

  set(_sources ${GAA_SOURCES})

  if(GAA_PROTOS)
    _goby_target_protoc_out_dir(_protoc_out_dir "${GAA_TARGET}" "${GAA_PROTOC_OUT_DIR}")
    _goby_target_generate_protos(_generated "${_protoc_out_dir}" "${GAA_PROTOS}"
      "${GAA_PROTO_IMPORT_DIRS}")
    list(APPEND _sources ${_generated})
  endif()

  add_executable(${GAA_TARGET} ${_sources})

  if(GAA_PROTOS)
    target_include_directories(${GAA_TARGET} PRIVATE "${_protoc_out_dir}")
  endif()

  target_link_libraries(${GAA_TARGET} goby ${GAA_LINK_LIBRARIES})

  install(TARGETS ${GAA_TARGET} RUNTIME DESTINATION bin)

  # goby_clang_tool, when the project asked for interface files and provides it
  if(export_goby_interfaces AND COMMAND generate_interfaces)
    generate_interfaces(TARGET ${GAA_TARGET})
  endif()
endfunction()

function(GOBY_ADD_LIBRARY)
  cmake_parse_arguments(GAL "STATIC;MODULE" "TARGET;PROTOC_OUT_DIR"
    "SOURCES;PROTOS;LINK_LIBRARIES;PROTO_IMPORT_DIRS" ${ARGN})

  if(NOT GAL_TARGET)
    message(FATAL_ERROR "goby_add_library: TARGET is required")
  endif()

  set(_lib_type SHARED)
  if(GAL_STATIC)
    set(_lib_type STATIC)
  elseif(GAL_MODULE)
    set(_lib_type MODULE)
  endif()

  set(_sources ${GAL_SOURCES})

  if(GAL_PROTOS)
    _goby_target_protoc_out_dir(_protoc_out_dir "${GAL_TARGET}" "${GAL_PROTOC_OUT_DIR}")
    _goby_target_generate_protos(_generated "${_protoc_out_dir}" "${GAL_PROTOS}"
      "${GAL_PROTO_IMPORT_DIRS}")
    list(APPEND _sources ${_generated})
  endif()

  add_library(${GAL_TARGET} ${_lib_type} ${_sources})

  if(GAL_PROTOS)
    target_include_directories(${GAL_TARGET} PUBLIC "${_protoc_out_dir}")
  endif()

  if(GAL_LINK_LIBRARIES)
    target_link_libraries(${GAL_TARGET} ${GAL_LINK_LIBRARIES})
  endif()
endfunction()
