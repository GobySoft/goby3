# GobyPython.cmake - build Goby applications written in Python
#
# Provides:
#   goby_add_python_app(TARGET <name>
#     INTERFACE_YML <file>
#     [PROTOS <proto_files>...]
#     [INCLUDES <headers>...]
#     [PROTO_MODULES <python.modules>...]
#     [LINK_LIBRARIES <libs>...]
#     [SOURCES <extra_cpp_files>...]
#     [OUTPUT_DIRECTORY <dir>])
#
# Reads INTERFACE_YML, generates the C++ glue code and the Python module that application code
# imports, and builds the glue into a pybind11 extension module. The interface file format is
# documented in share/goby/interface/README.md and is shared with the Julia bindings.
#
# Sets in the caller's scope:
#   <TARGET>_PYTHON_MODULE     name of the generated Python module to import
#   <TARGET>_PYTHON_DIRECTORY  directory holding the module and the extension; add to PYTHONPATH
#
# Requires pybind11 and the goby Python package (for goby_gen_cpp).

# Searching for the Python interpreter while cross-compiling is a hard error, not a quiet
# failure, unless CMAKE_CROSSCOMPILING_EMULATOR is set (CMake's FindPython, CMP0190). A Goby
# Python application has to be built natively anyway, so leave Python3_FOUND and pybind11_FOUND
# unset there: build_python then detects as OFF, which is what a cross build wants.
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
  find_package(Python3 COMPONENTS Interpreter Development.Module QUIET)

  if(Python3_FOUND AND NOT pybind11_DIR)
    # pybind11 is often installed with pip rather than as a system package, where CMake has no
    # reason to look for it; the module knows where it put itself
    execute_process(COMMAND ${Python3_EXECUTABLE} -m pybind11 --cmakedir
      OUTPUT_VARIABLE PYBIND11_PYTHON_CMAKE_DIR
      RESULT_VARIABLE PYBIND11_PYTHON_CMAKE_DIR_RESULT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(PYBIND11_PYTHON_CMAKE_DIR_RESULT EQUAL 0 AND IS_DIRECTORY "${PYBIND11_PYTHON_CMAKE_DIR}")
      set(pybind11_DIR "${PYBIND11_PYTHON_CMAKE_DIR}" CACHE PATH "The directory containing a CMake configuration file for pybind11.")
    endif()
  endif()

  find_package(pybind11 QUIET)
endif()

# The generator ships with the goby Python package. Prefer the console script; fall back to
# running the module out of the build or source tree, which is what an uninstalled build needs.
if(NOT GOBY_GEN_CPP_COMMAND)
  find_program(GOBY_GEN_CPP_EXECUTABLE goby_gen_cpp)
  if(GOBY_GEN_CPP_EXECUTABLE)
    set(GOBY_GEN_CPP_COMMAND ${GOBY_GEN_CPP_EXECUTABLE} CACHE STRING "Command that runs the Goby Python generator")
  elseif(Python3_EXECUTABLE AND EXISTS "${GOBY_PYTHON_SOURCE_DIR}/goby/gen.py")
    set(GOBY_GEN_CPP_COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${GOBY_PYTHON_SOURCE_DIR}
        ${Python3_EXECUTABLE} -m goby.gen
        CACHE STRING "Command that runs the Goby Python generator")
  endif()
endif()

# When the generator is being run out of a source tree rather than from an installed console
# script, changing it has to regenerate what it wrote
set(GOBY_GEN_CPP_DEPENDS)
if(EXISTS "${GOBY_PYTHON_SOURCE_DIR}/goby/gen.py")
  set(GOBY_GEN_CPP_DEPENDS "${GOBY_PYTHON_SOURCE_DIR}/goby/gen.py")
endif()

function(GOBY_ADD_PYTHON_APP)
  set(options)
  set(one_value_args TARGET INTERFACE_YML OUTPUT_DIRECTORY)
  set(multi_value_args PROTOS INCLUDES PROTO_MODULES LINK_LIBRARIES SOURCES)
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
  if(NOT GOBY_GEN_CPP_COMMAND)
    message(FATAL_ERROR
      "goby_add_python_app(${GAPA_TARGET}) requires the goby Python package: install it "
      "(python3-goby3) or set GOBY_GEN_CPP_COMMAND")
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

  pybind11_add_module(${GAPA_TARGET} "${_cpp_out}" ${GAPA_SOURCES} ${GAPA_PROTOS})

  set_target_properties(${GAPA_TARGET} PROPERTIES
    OUTPUT_NAME "${_module_name}"
    LIBRARY_OUTPUT_DIRECTORY "${_out_dir}")

  target_link_libraries(${GAPA_TARGET} PRIVATE ${GAPA_LINK_LIBRARIES})

  # the generated Python module is not compiled, but nothing else depends on it, so tie it to
  # the extension module's build
  add_custom_target(${GAPA_TARGET}_python_module ALL DEPENDS "${_python_out}")
  add_dependencies(${GAPA_TARGET} ${GAPA_TARGET}_python_module)

  set(${GAPA_TARGET}_PYTHON_MODULE "${_python_module}" PARENT_SCOPE)
  set(${GAPA_TARGET}_PYTHON_DIRECTORY "${_out_dir}" PARENT_SCOPE)
endfunction()
