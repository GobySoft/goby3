# GobyPython.cmake - build Goby applications written in Python
#
# Provides:
#   goby_add_python_app(TARGET <name>
#     INTERFACE_YML <file>
#     [MAIN <python_file>]
#     [SOURCES <files>...]
#     [INCLUDES <headers>...]
#     [LINK_LIBRARIES <libs>...]
#     [PYTHON_PROTOS <proto_files>...]
#     [PROTO_IMPORT_DIRS <dirs>...]
#     [PROTO_MODULES <python.modules>...]
#     [OUTPUT_DIRECTORY <dir>]
#     [LAUNCHER_DIRECTORY <dir>])
#
# Reads INTERFACE_YML, generates the C++ glue code and the Python module that application code
# imports, and builds the glue into a pybind11 extension module. The interface file format is
# documented in share/goby/interface/README.md and is shared with the Julia bindings.
#
# SOURCES are compiled into the extension module, which is where already-generated protobuf
# sources belong: Goby does not compile .proto files to C++ for you.
#
# PYTHON_PROTOS are compiled with protoc --python_out. A message's Python module path follows
# the path of its .proto relative to the first PROTO_IMPORT_DIRS entry containing it, so
#   PYTHON_PROTOS ${project_INC_DIR}/messages/nav.proto
#   PROTO_IMPORT_DIRS ${project_INC_DIR}
# gives messages.nav_pb2. Goby's own include directory is always searched, after these.
#
# MAIN writes a launcher named after TARGET into LAUNCHER_DIRECTORY, which runs it with the
# PYTHONPATH it needs. Omit MAIN for an application that is started another way.
#
# Sets in the caller's scope:
#   <TARGET>_PYTHON_MODULE      name of the generated Python module to import
#   <TARGET>_PYTHON_DIRECTORY   directory holding the module and the extension
#   <TARGET>_PYTHON_PROTO_DIR   directory holding the modules generated from PYTHON_PROTOS
#   <TARGET>_PYTHONPATH         all of the above, and Goby's own when it is not installed
#
# Requires pybind11 and the goby Python package (for goby_gen_cpp).
#
# Cross-compiling
# ---------------
# Two Pythons are involved and they are not the same one. The generators (goby_gen_cpp, protoc
# --python_out) run at build time, so they need the *host's* interpreter; the extension module is
# loaded by the *target's* interpreter, so it is compiled against the target's python3-dev and
# named with the target's ABI suffix. Install python3-dev for the target architecture
# (e.g. python3-dev:arm64) and this is detected from the toolchain; override it with:
#
#   GOBY_PYTHON_HOST_EXECUTABLE      host interpreter that runs the generators
#   GOBY_PYTHON_TARGET_VERSION       target Python version, e.g. 3.12 (defaults to the host's)
#   GOBY_PYTHON_TARGET_INCLUDE_DIRS  directories holding the target's Python.h and pyconfig.h
#   GOBY_PYTHON_TARGET_SOABI         extension suffix, e.g. cpython-312-aarch64-linux-gnu
#
# An extension module does not link libpython on Unix, so headers and the suffix are all that is
# needed; pybind11 is used headers-only so that it does not run its own Python search against the
# wrong architecture.

# Locates the target's Python headers and ABI suffix, which is all an extension module needs:
# on Unix it does not link libpython, so there is no target library to find.
function(GOBY_PYTHON_FIND_TARGET_DEVELOPMENT)
  if(GOBY_PYTHON_TARGET_INCLUDE_DIRS AND GOBY_PYTHON_TARGET_SOABI)
    return()
  endif()

  # Which version to build against is decided by what the target actually has, not by what the
  # host runs: the two are often the same distribution release but need not be, and picking the
  # host's version when the target has another only produces a confusing miss. The host's version
  # is tried first all the same, to disambiguate a target carrying several.
  set(_versions 3.15 3.14 3.13 3.12 3.11 3.10 3.9 3.8)
  if(GOBY_PYTHON_TARGET_VERSION)
    set(_versions "${GOBY_PYTHON_TARGET_VERSION}")
  elseif(GOBY_PYTHON_HOST_EXECUTABLE)
    execute_process(
      COMMAND "${GOBY_PYTHON_HOST_EXECUTABLE}" -c
              "import sys; print('%d.%d' % sys.version_info[:2])"
      OUTPUT_VARIABLE _host_version
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _host_version_result
      ERROR_QUIET)
    if(_host_version_result EQUAL 0 AND _host_version)
      list(INSERT _versions 0 "${_host_version}")
    endif()
  endif()

  # pyconfig.h is the architecture-dependent half, which multiarch keeps under the target triplet.
  # Deliberately never falling back to the shared python3.X/pyconfig.h when the triplet is known:
  # that one belongs to the host, and building against it would produce a module for the wrong
  # ABI rather than an error. A version counts only when both halves are present.
  set(_tried)
  foreach(_version ${_versions})
    if(CMAKE_LIBRARY_ARCHITECTURE)
      set(_config_suffix "${CMAKE_LIBRARY_ARCHITECTURE}/python${_version}")
    else()
      set(_config_suffix "python${_version}")
    endif()
    list(APPEND _tried "${_config_suffix}")

    find_path(_goby_python_h NAMES Python.h PATH_SUFFIXES "python${_version}")
    find_path(_goby_pyconfig_h NAMES pyconfig.h PATH_SUFFIXES "${_config_suffix}")

    if(_goby_python_h AND _goby_pyconfig_h)
      set(_found_version "${_version}")
      set(GOBY_PYTHON_TARGET_PYTHON_H_DIR "${_goby_python_h}"
        CACHE PATH "Directory holding the target's Python.h")
      set(GOBY_PYTHON_TARGET_PYCONFIG_H_DIR "${_goby_pyconfig_h}"
        CACHE PATH "Directory holding the target's pyconfig.h")
    endif()

    unset(_goby_python_h CACHE)
    unset(_goby_pyconfig_h CACHE)

    if(_found_version)
      break()
    endif()
  endforeach()

  if(NOT _found_version)
    string(REPLACE ";" ", " _tried_text "${_tried}")
    set(GOBY_PYTHON_TARGET_NOT_FOUND_REASON
      "no target Python headers found: looked for Python.h and a matching pyconfig.h under \
${_tried_text}. Install python3-dev for the target architecture (e.g. python3-dev:arm64), or set \
GOBY_PYTHON_TARGET_VERSION" PARENT_SCOPE)
    return()
  endif()

  string(REPLACE "." ";" _version_parts "${_found_version}")
  list(GET _version_parts 0 _major)
  list(GET _version_parts 1 _minor)

  set(_include_dirs "${GOBY_PYTHON_TARGET_PYTHON_H_DIR}")
  if(NOT "${GOBY_PYTHON_TARGET_PYCONFIG_H_DIR}" STREQUAL "${GOBY_PYTHON_TARGET_PYTHON_H_DIR}")
    list(APPEND _include_dirs "${GOBY_PYTHON_TARGET_PYCONFIG_H_DIR}")
  endif()

  if(NOT GOBY_PYTHON_TARGET_SOABI)
    if(NOT CMAKE_LIBRARY_ARCHITECTURE)
      set(GOBY_PYTHON_TARGET_NOT_FOUND_REASON
        "CMAKE_LIBRARY_ARCHITECTURE is not set, so the extension module suffix cannot be \
derived; set GOBY_PYTHON_TARGET_SOABI (e.g. cpython-${_major}${_minor}-aarch64-linux-gnu)"
        PARENT_SCOPE)
      return()
    endif()
    set(GOBY_PYTHON_TARGET_SOABI "cpython-${_major}${_minor}-${CMAKE_LIBRARY_ARCHITECTURE}"
      CACHE STRING "ABI suffix of the target's extension modules")
  endif()

  set(GOBY_PYTHON_TARGET_INCLUDE_DIRS "${_include_dirs}"
    CACHE STRING "Include directories for the target's Python headers")
  set(GOBY_PYTHON_TARGET_NOT_FOUND_REASON "" PARENT_SCOPE)
endfunction()

set(GOBY_PYTHON_CROSSCOMPILING FALSE)
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  set(GOBY_PYTHON_CROSSCOMPILING TRUE)
endif()

if(NOT GOBY_PYTHON_CROSSCOMPILING)
  find_package(Python3 COMPONENTS Interpreter Development.Module QUIET)
  set(GOBY_PYTHON_HOST_EXECUTABLE "${Python3_EXECUTABLE}")
else()
  # FindPython would search the target for both, and makes the interpreter a hard error when
  # cross-compiling without an emulator (CMP0190), so each half is found on its own
  if(NOT GOBY_PYTHON_HOST_EXECUTABLE)
    find_program(GOBY_PYTHON_HOST_EXECUTABLE NAMES python3 python
      NO_CMAKE_FIND_ROOT_PATH
      DOC "Host Python interpreter that runs the Goby generators when cross-compiling")
  endif()
  goby_python_find_target_development()
endif()

if(GOBY_PYTHON_HOST_EXECUTABLE AND NOT pybind11_DIR)
  # a pip-installed pybind11 is somewhere CMake has no reason to look; the module knows where
  execute_process(COMMAND ${GOBY_PYTHON_HOST_EXECUTABLE} -m pybind11 --cmakedir
    OUTPUT_VARIABLE PYBIND11_PYTHON_CMAKE_DIR
    RESULT_VARIABLE PYBIND11_PYTHON_CMAKE_DIR_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(PYBIND11_PYTHON_CMAKE_DIR_RESULT EQUAL 0 AND IS_DIRECTORY "${PYBIND11_PYTHON_CMAKE_DIR}")
    set(pybind11_DIR "${PYBIND11_PYTHON_CMAKE_DIR}" CACHE PATH "The directory containing a CMake configuration file for pybind11.")
  endif()
endif()

if(GOBY_PYTHON_CROSSCOMPILING)
  # tells pybind11Config to provide pybind11::headers without searching for Python itself
  set(PYBIND11_NOPYTHON ON)
endif()
find_package(pybind11 QUIET)

# The generator ships with the goby Python package. Prefer the console script; fall back to
# running the module out of the build or source tree, which is what an uninstalled build needs.
if(NOT GOBY_GEN_CPP_COMMAND)
  find_program(GOBY_GEN_CPP_EXECUTABLE goby_gen_cpp)
  if(GOBY_GEN_CPP_EXECUTABLE)
    set(GOBY_GEN_CPP_COMMAND ${GOBY_GEN_CPP_EXECUTABLE} CACHE STRING "Command that runs the Goby Python generator")
  elseif(GOBY_PYTHON_HOST_EXECUTABLE AND EXISTS "${GOBY_PYTHON_SOURCE_DIR}/goby/gen.py")
    set(GOBY_GEN_CPP_COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}
        ${GOBY_PYTHON_HOST_EXECUTABLE} -m goby.gen
        CACHE STRING "Command that runs the Goby Python generator")
  endif()
endif()

# a generator run out of the source tree has to regenerate what it wrote when it changes
set(GOBY_GEN_CPP_DEPENDS)
if(EXISTS "${GOBY_PYTHON_SOURCE_DIR}/goby/gen.py")
  set(GOBY_GEN_CPP_DEPENDS "${GOBY_PYTHON_SOURCE_DIR}/goby/gen.py")
endif()

find_program(GOBY_PROTOC_EXECUTABLE NAMES protoc)
# AppConfig imports dccl/option_extensions.proto, so any application configuration needs it
find_path(GOBY_DCCL_PROTO_DIR dccl/option_extensions.proto)

# The include directory holding Goby's own .proto files, in this project or in a consumer of it.
# Resolved when an application is declared rather than here: neither variable is necessarily set
# by the time this module is included.
function(GOBY_RESOLVE_PROTO_DIR out_var)
  if(GOBY_PROTO_DIR)
    set(${out_var} "${GOBY_PROTO_DIR}" PARENT_SCOPE)
  elseif(goby_INC_DIR)
    set(${out_var} "${goby_INC_DIR}" PARENT_SCOPE)
  else()
    set(${out_var} "${GOBY_INCLUDE_DIR}" PARENT_SCOPE)
  endif()
endfunction()

function(GOBY_ADD_PYTHON_APP)
  set(options)
  set(one_value_args TARGET INTERFACE_YML MAIN OUTPUT_DIRECTORY LAUNCHER_DIRECTORY)
  set(multi_value_args SOURCES INCLUDES LINK_LIBRARIES PYTHON_PROTOS PROTO_IMPORT_DIRS PROTO_MODULES)
  cmake_parse_arguments(GAPA "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT GAPA_TARGET)
    message(FATAL_ERROR "goby_add_python_app: TARGET is required")
  endif()
  if(NOT GAPA_INTERFACE_YML)
    message(FATAL_ERROR "goby_add_python_app: INTERFACE_YML is required")
  endif()
  if(NOT pybind11_FOUND)
    message(FATAL_ERROR "goby_add_python_app(${GAPA_TARGET}) requires pybind11 (pybind11-dev)")
  endif()
  if(GOBY_PYTHON_CROSSCOMPILING AND NOT GOBY_PYTHON_TARGET_INCLUDE_DIRS)
    message(FATAL_ERROR "goby_add_python_app(${GAPA_TARGET}) is cross-compiling but "
      "${GOBY_PYTHON_TARGET_NOT_FOUND_REASON}")
  endif()
  if(NOT GOBY_GEN_CPP_COMMAND)
    message(FATAL_ERROR
      "goby_add_python_app(${GAPA_TARGET}) requires the goby Python package: install it "
      "(python3-goby3) or set GOBY_GEN_CPP_COMMAND")
  endif()
  if(GAPA_PYTHON_PROTOS AND NOT GOBY_PROTOC_EXECUTABLE)
    message(FATAL_ERROR "goby_add_python_app(${GAPA_TARGET}): PYTHON_PROTOS needs protoc, which was not found")
  endif()

  get_filename_component(_interface_yml "${GAPA_INTERFACE_YML}" ABSOLUTE)

  if(GAPA_OUTPUT_DIRECTORY)
    set(_out_dir "${GAPA_OUTPUT_DIRECTORY}")
  else()
    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/${GAPA_TARGET}")
  endif()
  file(MAKE_DIRECTORY "${_out_dir}")

  # ask the generator for the extension module name, so that CMake and the generated Python
  # module agree on it without either having to reimplement the naming rule
  execute_process(
    COMMAND ${GOBY_GEN_CPP_COMMAND} "${_interface_yml}" --print-module-name
    OUTPUT_VARIABLE _module_name
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _module_name_result
    ERROR_VARIABLE _module_name_error)
  if(NOT _module_name_result EQUAL 0)
    message(FATAL_ERROR "goby_add_python_app(${GAPA_TARGET}): could not read ${_interface_yml}:\n${_module_name_error}")
  endif()

  string(REGEX REPLACE "^_" "" _python_module "${_module_name}")

  set(_cpp_out "${_out_dir}/${GAPA_TARGET}.cpp")
  set(_python_out "${_out_dir}/${_python_module}.py")

  set(_include_args)
  foreach(_include ${GAPA_INCLUDES})
    list(APPEND _include_args --include "${_include}")
  endforeach()

  set(_proto_module_args)
  foreach(_proto_module ${GAPA_PROTO_MODULES})
    list(APPEND _proto_module_args --proto-module "${_proto_module}")
  endforeach()

  add_custom_command(
    OUTPUT "${_cpp_out}" "${_python_out}"
    COMMAND ${GOBY_GEN_CPP_COMMAND} "${_interface_yml}"
            --cpp-out "${_cpp_out}"
            --python-out "${_python_out}"
            --module "${_module_name}"
            ${_include_args} ${_proto_module_args}
    DEPENDS "${_interface_yml}" ${GOBY_GEN_CPP_DEPENDS}
    COMMENT "Generating Goby Python bindings for ${GAPA_TARGET} from ${GAPA_INTERFACE_YML}"
    VERBATIM)

  if(GOBY_PYTHON_CROSSCOMPILING)
    # pybind11_add_module needs the Python that pybind11Config was told not to look for, so the
    # module is assembled here from pybind11::headers and the target's own headers and suffix
    add_library(${GAPA_TARGET} MODULE "${_cpp_out}" ${GAPA_SOURCES})
    target_link_libraries(${GAPA_TARGET} PRIVATE pybind11::headers)
    target_include_directories(${GAPA_TARGET} SYSTEM PRIVATE ${GOBY_PYTHON_TARGET_INCLUDE_DIRS})
    set_target_properties(${GAPA_TARGET} PROPERTIES
      PREFIX ""
      SUFFIX ".${GOBY_PYTHON_TARGET_SOABI}.so"
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  else()
    pybind11_add_module(${GAPA_TARGET} "${_cpp_out}" ${GAPA_SOURCES})
  endif()

  set_target_properties(${GAPA_TARGET} PROPERTIES
    OUTPUT_NAME "${_module_name}"
    LIBRARY_OUTPUT_DIRECTORY "${_out_dir}")

  target_link_libraries(${GAPA_TARGET} PRIVATE ${GAPA_LINK_LIBRARIES})

  # the generated Python module is not compiled, but nothing else depends on it, so tie it to
  # the extension module's build
  add_custom_target(${GAPA_TARGET}_python_module ALL DEPENDS "${_python_out}")
  add_dependencies(${GAPA_TARGET} ${GAPA_TARGET}_python_module)

  set(_proto_dir "${_out_dir}/proto")

  if(GAPA_PYTHON_PROTOS)
    goby_resolve_proto_dir(_goby_proto_dir)
    if(NOT _goby_proto_dir)
      message(FATAL_ERROR "goby_add_python_app: cannot find Goby's .proto files, which every "
        "application configuration imports. Set GOBY_PROTO_DIR to the directory holding them.")
    endif()

    file(MAKE_DIRECTORY "${_proto_dir}")

    # the caller's directories first: they decide the module paths. Existence is deliberately not
    # checked: these are usually build tree directories that nothing has written to yet when the
    # application is declared, and dropping one silently moves the module protoc writes.
    set(_import_dirs ${GAPA_PROTO_IMPORT_DIRS} ${_goby_proto_dir} ${GOBY_DCCL_PROTO_DIR})
    set(_import_args)
    set(_seen_dirs)
    foreach(_dir ${_import_dirs})
      if("${_dir}" STREQUAL "" OR "${_dir}" MATCHES "NOTFOUND$")
        continue()
      endif()
      get_filename_component(_abs_dir "${_dir}" ABSOLUTE)
      if(NOT "${_abs_dir}" IN_LIST _seen_dirs)
        list(APPEND _seen_dirs "${_abs_dir}")
        list(APPEND _import_args -I "${_abs_dir}")
      endif()
    endforeach()

    set(_proto_outs)
    foreach(_proto ${GAPA_PYTHON_PROTOS})
      get_filename_component(_abs_proto "${_proto}" ABSOLUTE)
      get_filename_component(_proto_dir_of "${_abs_proto}" DIRECTORY)

      get_filename_component(_proto_we "${_abs_proto}" NAME_WE)
      set(_relative_out "${_proto_we}_pb2.py")
      foreach(_import_dir ${_seen_dirs})
        file(RELATIVE_PATH _relative "${_import_dir}" "${_abs_proto}")
        if(NOT _relative MATCHES "^\\.\\.")
          string(REGEX REPLACE "\\.proto$" "_pb2.py" _relative_out "${_relative}")
          break()
        endif()
      endforeach()

      set(_proto_out "${_proto_dir}/${_relative_out}")
      add_custom_command(
        OUTPUT "${_proto_out}"
        COMMAND ${GOBY_PROTOC_EXECUTABLE}
                --python_out "${_proto_dir}"
                ${_import_args} -I "${_proto_dir_of}"
                "${_abs_proto}"
        DEPENDS "${_abs_proto}"
        COMMENT "Running protoc (python) on ${_proto}"
        VERBATIM)
      list(APPEND _proto_outs "${_proto_out}")
    endforeach()

    add_custom_target(${GAPA_TARGET}_python_protos ALL DEPENDS ${_proto_outs})
    add_dependencies(${GAPA_TARGET} ${GAPA_TARGET}_python_protos)
  endif()

  set(_pythonpath "${_out_dir}")
  if(GAPA_PYTHON_PROTOS)
    set(_pythonpath "${_pythonpath}:${_proto_dir}")
  endif()
  # an uninstalled Goby keeps its package and generated protobuf modules in its own source and
  # build trees; both are already importable once python3-goby3 is installed
  foreach(_goby_python_dir "${GOBY_PYTHON_SOURCE_DIR}" "${GOBY_PYTHON_PROTO_DIR}")
    if(_goby_python_dir AND IS_DIRECTORY "${_goby_python_dir}")
      set(_pythonpath "${_pythonpath}:${_goby_python_dir}")
    endif()
  endforeach()

  if(GAPA_MAIN)
    if(GAPA_LAUNCHER_DIRECTORY)
      set(_launcher_dir "${GAPA_LAUNCHER_DIRECTORY}")
    else()
      set(_launcher_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    if(NOT _launcher_dir)
      message(FATAL_ERROR
        "goby_add_python_app(${GAPA_TARGET}): MAIN needs somewhere to write the launcher. Set "
        "CMAKE_RUNTIME_OUTPUT_DIRECTORY or pass LAUNCHER_DIRECTORY.")
    endif()

    get_filename_component(_main "${GAPA_MAIN}" ABSOLUTE)

    # the launcher runs on the target, so a cross build must not bake in the host's interpreter
    if(GOBY_PYTHON_CROSSCOMPILING)
      set(_launcher_python "python3")
    else()
      set(_launcher_python "${Python3_EXECUTABLE}")
    endif()

    file(GENERATE
      OUTPUT "${_launcher_dir}/${GAPA_TARGET}"
      CONTENT "#!/bin/sh
PYTHONPATH=\"${_pythonpath}\${PYTHONPATH:+:\$PYTHONPATH}\"
export PYTHONPATH
exec \"${_launcher_python}\" \"${_main}\" \"$@\"
"
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  endif()

  set(${GAPA_TARGET}_PYTHON_MODULE "${_python_module}" PARENT_SCOPE)
  set(${GAPA_TARGET}_PYTHON_DIRECTORY "${_out_dir}" PARENT_SCOPE)
  set(${GAPA_TARGET}_PYTHON_PROTO_DIR "${_proto_dir}" PARENT_SCOPE)
  set(${GAPA_TARGET}_PYTHONPATH "${_pythonpath}" PARENT_SCOPE)
endfunction()
