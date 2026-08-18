# GobyJulia.cmake - build Goby applications written in Julia
#
# Include it explicitly (find_package(GOBY) puts Goby's module directory on CMAKE_MODULE_PATH):
#   include(GobyJulia)
#   if(GOBY_JULIA_FOUND)
#     goby_add_julia_app(...)
#   endif()
#
# Provides:
#   goby_add_julia_app(TARGET <name>
#     INTERFACE_YML <file>
#     MAIN <julia_file>
#     [SOURCES <files>...]
#     [INCLUDES <headers>...]
#     [LINK_LIBRARIES <libs>...]
#     [THREADS <n>]
#     [OUTPUT_DIRECTORY <dir>]
#     [LAUNCHER_DIRECTORY <dir>])
#
#   goby_julia_proto_include_dirs(<dirs>...)
#   goby_add_julia_protos(PACKAGE <julia_package> PROTOS <proto_files>...)
#   goby_generate_julia_protos()
#
# Reads INTERFACE_YML, generates the C++ bridge with Goby.jl's gen_goby.jl, and builds it into
# the shared library Julia dlopens. SOURCES are compiled into it, which is where
# already-generated protobuf sources belong: Goby does not compile .proto files to C++ for you.
#
# Also writes <TARGET>_goby.jl beside the library, defining module <application name>Goby with
# the declared groups and one accessor per portal, so an application names its groups instead of
# repeating the interface.yml expression as a string:
#
#   include(joinpath(@__DIR__, "my_app_goby.jl"))
#   Goby.subscribe(app, MyAppGoby.interprocess(), MyAppGoby.groups.nav, callback)
#
# THREADS is the number of Julia threads the launcher starts the application with, which an
# application using Goby.run()'s task modules needs: one per task module plus three, for Main,
# the loop timer and the C++ application. It is a default, so JULIA_NUM_THREADS still wins.
#
# Exports <TARGET>_JULIA_DIRECTORY and <TARGET>_JULIA_MODULE.
#
# ProtoBuf.jl has to see every .proto at once to give them consistent modules, so the Julia
# protobuf bindings are generated for the project rather than per application: declare them with
# goby_add_julia_protos() and generate them with goby_generate_julia_protos(). On CMake 3.19 and
# later that last call is made automatically at the end of the top level directory.
#
# Sets:
#   GOBY_JULIA_FOUND         whether the pieces needed to build a Julia application were found
#   GOBY_JULIA_NOT_FOUND_REASON  why not, when it is FALSE
#   GOBY_JULIA_DIR           the Goby.jl project used to run the generators
#
# Requires julia and CxxWrap.jl:
#   julia -e 'import Pkg; Pkg.add("CxxWrap")'

if(GOBY_JULIA_INCLUDED)
  return()
endif()
set(GOBY_JULIA_INCLUDED TRUE)

set(GOBY_JULIA_BUILD_DIR "${CMAKE_BINARY_DIR}/julia" CACHE PATH "Directory for the generated Julia code")

# Detection reports through GOBY_JULIA_FOUND rather than returning, so that including this from
# another module cannot cut that module short
set(GOBY_JULIA_FOUND FALSE)
set(GOBY_JULIA_NOT_FOUND_REASON "")

if(NOT JULIA)
  find_program(JULIA julia)
endif()

# Goby.jl ships in Goby's share directory, beside the include directory in both an installed and
# an uninstalled Goby
if(goby_INC_DIR)
  set(_goby_julia_inc "${goby_INC_DIR}")
else()
  set(_goby_julia_inc "${GOBY_INCLUDE_DIR}")
endif()
get_filename_component(GOBY_JULIA_SRC_DIR "${_goby_julia_inc}/../share/goby/Goby.jl" ABSOLUTE)

if(NOT JULIA)
  set(GOBY_JULIA_NOT_FOUND_REASON "julia was not found on the PATH (set JULIA to the executable)")
elseif(NOT EXISTS "${GOBY_JULIA_SRC_DIR}/Project.toml")
  set(GOBY_JULIA_NOT_FOUND_REASON "Goby.jl not found at ${GOBY_JULIA_SRC_DIR}")
else()
  set(GOBY_JULIA_DIR "${GOBY_JULIA_BUILD_DIR}/Goby.jl")
  set(GOBY_JULIA_MANIFEST "${GOBY_JULIA_DIR}/Manifest.toml")
  # copied so that Julia can write Manifest.toml next to it
  file(COPY "${GOBY_JULIA_SRC_DIR}/src" DESTINATION "${GOBY_JULIA_DIR}")
  file(COPY "${GOBY_JULIA_SRC_DIR}/Project.toml" DESTINATION "${GOBY_JULIA_DIR}")

  if(NOT DEFINED CXXWRAP_PREFIX)
    execute_process(
      COMMAND "${JULIA}" -e "using CxxWrap; println(CxxWrap.prefix_path())"
      OUTPUT_VARIABLE _cxxwrap_prefix
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _cxxwrap_result)
    if(_cxxwrap_result EQUAL 0)
      set(CXXWRAP_PREFIX "${_cxxwrap_prefix}" CACHE PATH "Prefix path for JlCxx from Julia")
    endif()
  endif()

  if(NOT CXXWRAP_PREFIX)
    set(GOBY_JULIA_NOT_FOUND_REASON "CxxWrap.jl is not installed in Julia")
  else()
    find_package(JlCxx PATHS "${CXXWRAP_PREFIX}" QUIET)
    if(NOT JlCxx_FOUND)
      set(GOBY_JULIA_NOT_FOUND_REASON "JlCxx not found at ${CXXWRAP_PREFIX}")
    else()
      get_target_property(GOBY_JLCXX_LOCATION JlCxx::cxxwrap_julia LOCATION)
      get_filename_component(GOBY_JLCXX_LOCATION "${GOBY_JLCXX_LOCATION}" DIRECTORY)
      set(GOBY_JULIA_FOUND TRUE)
    endif()
  endif()
endif()

if(NOT GOBY_JULIA_FOUND)
  message(STATUS "Goby Julia support unavailable: ${GOBY_JULIA_NOT_FOUND_REASON}")
  return()
endif()

message(STATUS "Found Goby.jl at ${GOBY_JULIA_SRC_DIR}, JlCxx at ${GOBY_JLCXX_LOCATION}")

# One target rather than one rule per application, so that two julia processes never write
# Manifest.toml at once during a parallel build
add_custom_command(
  OUTPUT "${GOBY_JULIA_MANIFEST}"
  COMMAND "${JULIA}"
  ARGS --project="${GOBY_JULIA_DIR}" -L "${GOBY_JULIA_DIR}/src/pkg.jl" -e "'install_pkgs()'"
  COMMENT "Installing Julia packages for Goby.jl")
add_custom_target(goby_julia_pkgs DEPENDS "${GOBY_JULIA_MANIFEST}")

unset(GOBY_JULIA_PROTOS CACHE)
unset(GOBY_JULIA_PROTO_INCLUDES CACHE)
unset(GOBY_JULIA_PROTO_DEPENDS CACHE)
unset(GOBY_JULIA_PROTO_OUTPUT CACHE)

function(GOBY_ADD_JULIA_APP)
  set(one_value_args TARGET INTERFACE_YML MAIN THREADS OUTPUT_DIRECTORY LAUNCHER_DIRECTORY)
  set(multi_value_args SOURCES INCLUDES LINK_LIBRARIES)
  cmake_parse_arguments(GAJA "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  foreach(_required TARGET INTERFACE_YML MAIN)
    if(NOT GAJA_${_required})
      message(FATAL_ERROR "goby_add_julia_app: ${_required} is required")
    endif()
  endforeach()

  if(GAJA_OUTPUT_DIRECTORY)
    set(_out_dir "${GAJA_OUTPUT_DIRECTORY}")
  else()
    set(_out_dir "${GOBY_JULIA_BUILD_DIR}/${GAJA_TARGET}")
  endif()
  file(MAKE_DIRECTORY "${_out_dir}")

  # the application runs from the build directory, beside the library it loads
  get_filename_component(_main_name "${GAJA_MAIN}" NAME)
  configure_file("${GAJA_MAIN}" "${_out_dir}/${_main_name}" COPYONLY)

  get_filename_component(_interface_yml "${GAJA_INTERFACE_YML}" ABSOLUTE)

  set(_cpp_out_dir "${GOBY_JULIA_BUILD_DIR}/c++")
  file(MAKE_DIRECTORY "${_cpp_out_dir}")
  set(_cpp_out "${_cpp_out_dir}/${GAJA_TARGET}.cpp")

  set(_include_str "")
  foreach(_include ${GAJA_INCLUDES})
    string(APPEND _include_str "\"${_include}\",")
  endforeach()

  # the module an application includes for its groups and layer accessors; written beside the
  # library from the same generator run, so one julia startup produces both sides
  set(_jl_out "${_out_dir}/${GAJA_TARGET}_goby.jl")

  add_custom_command(
    OUTPUT "${_cpp_out}" "${_jl_out}"
    DEPENDS "${_interface_yml}"
    COMMAND "${JULIA}"
    ARGS --project=${GOBY_JULIA_DIR}
         -L "${GOBY_JULIA_DIR}/src/gen_goby.jl"
         # two -e rather than one statement separated by ';', which CMake splits into a list
         -e "'goby_gen_cpp(\"${_interface_yml}\",\"${_cpp_out}\",[${_include_str}])'"
         -e "'goby_gen_julia(\"${_interface_yml}\",\"${_jl_out}\")'"
    COMMENT "Generating Goby Julia bindings for ${GAJA_TARGET} from ${GAJA_INTERFACE_YML}")

  add_library(${GAJA_TARGET} SHARED "${_cpp_out}" ${GAJA_SOURCES})

  # gen_goby.jl needs Goby.jl's dependencies installed first
  add_dependencies(${GAJA_TARGET} goby_julia_pkgs)

  set_target_properties(${GAJA_TARGET} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${_out_dir}"
    # julia dlopens the library, which has to find libcxxwrap_julia
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${GOBY_JLCXX_LOCATION}")

  target_link_libraries(${GAJA_TARGET} JlCxx::cxxwrap_julia ${GAJA_LINK_LIBRARIES})

  # C++ 20 warns on implicit lambda capture of this for [=]. Spelled -Wdeprecated by GCC and
  # -Wdeprecated-this-capture by clang, but the group covers it on both and an unknown warning
  # name is a hard error, so the group is the portable one to name.
  target_compile_options(${GAJA_TARGET} PRIVATE -Wno-error=deprecated)


  # Julia fixes its thread count at startup, so an application using Goby.run()'s task modules
  # has to be launched with enough: one per task module plus three, for Main, the loop timer and
  # the C++ application. Written as a default so an operator can still raise it.
  set(_thread_arg "")
  if(GAJA_THREADS)
    if(NOT GAJA_THREADS MATCHES "^([1-9][0-9]*|auto)$")
      message(FATAL_ERROR "goby_add_julia_app: THREADS must be a positive number or 'auto', "
        "got '${GAJA_THREADS}'")
    endif()
    set(_thread_arg " -t \"\${JULIA_NUM_THREADS:-${GAJA_THREADS}}\"")
  endif()

  if(GAJA_LAUNCHER_DIRECTORY)
    set(_launcher_dir "${GAJA_LAUNCHER_DIRECTORY}")
  else()
    set(_launcher_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  endif()
  if(_launcher_dir)
    file(GENERATE
      OUTPUT "${_launcher_dir}/${GAJA_TARGET}"
      CONTENT "#!/bin/sh
exec \"${JULIA}\"${_thread_arg} \"${_out_dir}/${_main_name}\" \"$@\"
"
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  endif()

  set(${GAJA_TARGET}_JULIA_DIRECTORY "${_out_dir}" PARENT_SCOPE)
  set(${GAJA_TARGET}_JULIA_MODULE "${_jl_out}" PARENT_SCOPE)
endfunction()

function(GOBY_JULIA_PROTO_INCLUDE_DIRS)
  if(NOT ARGN)
    message(SEND_ERROR "goby_julia_proto_include_dirs() called without any directories")
    return()
  endif()

  foreach(_dir ${ARGN})
    set(GOBY_JULIA_PROTO_INCLUDES "${GOBY_JULIA_PROTO_INCLUDES},\"${_dir}\"" CACHE INTERNAL "Julia proto includes")
  endforeach()
endfunction()

function(GOBY_ADD_JULIA_PROTOS)
  cmake_parse_arguments(GAJP "" "PACKAGE" "PROTOS" ${ARGN})

  if(NOT GAJP_PACKAGE)
    message(FATAL_ERROR "goby_add_julia_protos: PACKAGE is required")
  endif()
  if(NOT GAJP_PROTOS)
    message(FATAL_ERROR "goby_add_julia_protos: PROTOS is required")
  endif()

  file(MAKE_DIRECTORY ${GOBY_JULIA_BUILD_DIR})
  goby_julia_proto_include_dirs(${CMAKE_CURRENT_SOURCE_DIR})

  foreach(_proto ${GAJP_PROTOS})
    get_filename_component(_abs_proto ${_proto} ABSOLUTE)
    get_filename_component(_proto_we ${_abs_proto} NAME_WE)
    # ProtoBuf.jl writes one <name>_pb.jl per proto into a tree mirroring the proto's package,
    # which cannot be derived here without parsing it. The top-level module file is generated
    # for every package and is what the Julia scripts include, so track that instead.
    set(_proto_jl_out "${GOBY_JULIA_BUILD_DIR}/${GAJP_PACKAGE}/${GAJP_PACKAGE}.jl")

    set(GOBY_JULIA_PROTOS "${GOBY_JULIA_PROTOS},\"${_proto_we}.proto\"" CACHE INTERNAL "Julia protos")
    set(GOBY_JULIA_PROTO_OUTPUT "${GOBY_JULIA_PROTO_OUTPUT};${_proto_jl_out}" CACHE INTERNAL "Julia proto outputs")
    set(GOBY_JULIA_PROTO_DEPENDS "${GOBY_JULIA_PROTO_DEPENDS};${_abs_proto}" CACHE INTERNAL "Julia proto dependencies")
  endforeach()

  # CMake runs this itself at the end of the top level directory, so that a project does not
  # have to remember to; older CMake needs the explicit call. The guard is a global property,
  # not a cache entry: a deferred call lasts for one configure run, so it must be registered
  # again on the next one.
  if(NOT CMAKE_VERSION VERSION_LESS 3.19)
    get_property(_deferred GLOBAL PROPERTY GOBY_JULIA_PROTOS_DEFERRED)
    if(NOT _deferred)
      set_property(GLOBAL PROPERTY GOBY_JULIA_PROTOS_DEFERRED TRUE)
      cmake_language(EVAL CODE
        "cmake_language(DEFER DIRECTORY \"${CMAKE_SOURCE_DIR}\" CALL goby_generate_julia_protos)")
    endif()
  endif()
endfunction()

macro(GOBY_GENERATE_JULIA_PROTOS)
  if(GOBY_JULIA_PROTO_OUTPUT AND NOT TARGET goby_julia_protos)
    # the accumulators start with a separator
    list(REMOVE_AT GOBY_JULIA_PROTO_OUTPUT 0)
    list(REMOVE_AT GOBY_JULIA_PROTO_DEPENDS 0)
    # several protos may share a package, and so a top-level module file
    list(REMOVE_DUPLICATES GOBY_JULIA_PROTO_OUTPUT)

    string(SUBSTRING "${GOBY_JULIA_PROTO_INCLUDES}" 1 -1 GOBY_JULIA_PROTO_INCLUDES)
    string(SUBSTRING "${GOBY_JULIA_PROTOS}" 1 -1 GOBY_JULIA_PROTOS)

    set(GOBY_JULIA_PROTO_STAMP "${GOBY_JULIA_BUILD_DIR}/.protos.stamp")

    add_custom_command(
      OUTPUT ${GOBY_JULIA_PROTO_OUTPUT}
      BYPRODUCTS ${GOBY_JULIA_PROTO_STAMP}
      DEPENDS ${GOBY_JULIA_PROTO_DEPENDS}
      COMMAND ${JULIA}
      ARGS --project=${GOBY_JULIA_DIR} -L ${GOBY_JULIA_DIR}/src/gen_goby.jl -e "'gen_proto([${GOBY_JULIA_PROTOS}],[${GOBY_JULIA_PROTO_INCLUDES}],\"${GOBY_JULIA_BUILD_DIR}\",\"${GOBY_JULIA_PROTO_STAMP}\")'"
      COMMENT "Running the Julia protocol buffer compiler on all project protos")

    add_custom_target(goby_julia_protos ALL DEPENDS ${GOBY_JULIA_PROTO_OUTPUT})
    add_dependencies(goby_julia_protos goby_julia_pkgs)
  endif()
endmacro()
