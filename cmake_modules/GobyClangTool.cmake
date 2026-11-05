# must output json compile commands for goby_clang_tool to work
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Enable/Disable output of compile commands during generation." FORCE)

# usage: goby_export_interface(target_name ${OUTPUT_DIR} YML)
# sets YML to path to yml file
function(GOBY_EXPORT_INTERFACE TARGET YML_OUT_DIR STUB_OUT_DIR YML)
  get_target_property(TARGET_SOURCES ${TARGET} SOURCES)

  file(MAKE_DIRECTORY ${YML_OUT_DIR})  
  
  set(ABS_TARGET_SOURCES)
  foreach(SOURCE ${TARGET_SOURCES})
    get_filename_component(ABS_TARGET_SOURCE ${SOURCE} ABSOLUTE)
    get_filename_component(SOURCE_EXTENSION ${SOURCE} EXT)

    # omit protobuf header files
    if(NOT SOURCE_EXTENSION STREQUAL ".pb.h")
      list(APPEND ABS_TARGET_SOURCES ${ABS_TARGET_SOURCE})
    endif()
  endforeach()  
  
  set(${YML} "${YML_OUT_DIR}/${TARGET}_interface.yml")
  add_custom_command(
    OUTPUT "${YML_OUT_DIR}/${TARGET}_interface.yml"
    COMMAND goby_clang_tool
    ARGS -gen -target ${TARGET} -outdir ${YML_OUT_DIR} -p ${CMAKE_BINARY_DIR} ${ABS_TARGET_SOURCES}
    COMMENT "Running goby_clang_tool (gen) on ${TARGET}"
    DEPENDS ${ABS_TARGET_SOURCES} ${TARGET}
    VERBATIM)
  
  set_source_files_properties(${${YML}} PROPERTIES GENERATED TRUE)
  set(${YML} ${${YML}} PARENT_SCOPE)

  if(NOT "${STUB_OUT_DIR}" STREQUAL "")
    file(MAKE_DIRECTORY ${STUB_OUT_DIR})


    # create stub image
    set(STUB_DOT_OUT "${TARGET}_stub_deployment.dot")
    add_custom_command(
      OUTPUT ${STUB_OUT_DIR}/${STUB_DOT_OUT}
      COMMAND goby_clang_tool
      ARGS -viz -include-all -outdir ${STUB_OUT_DIR} -o ${STUB_DOT_OUT} ${${YML}}
      COMMENT "Running goby_clang_tool (viz) on ${TARGET}"
      DEPENDS ${${YML}}
      VERBATIM)
    
    set_source_files_properties(${STUB_OUT_DIR}/${STUB_DOT_OUT} PROPERTIES GENERATED TRUE)
    
    
    set(STUB_PNG_OUT "${TARGET}_stub_deployment.png")
    add_custom_command(
      OUTPUT ${STUB_OUT_DIR}/${STUB_PNG_OUT}
      COMMAND dot
      ARGS -Tpng -o ${STUB_OUT_DIR}/${STUB_PNG_OUT} ${STUB_OUT_DIR}/${STUB_DOT_OUT}
      DEPENDS ${STUB_OUT_DIR}/${STUB_DOT_OUT}
      )
    
    set_source_files_properties(${STUB_OUT_DIR}/${STUB_PNG_OUT} PROPERTIES GENERATED TRUE)
    
    add_custom_target(${TARGET}_stub_interface_viz ALL DEPENDS ${STUB_OUT_DIR}/${STUB_PNG_OUT})
  else()
    add_custom_target(${TARGET}_interface ALL DEPENDS ${${YML}})    
  endif()
    
endfunction()

# usage: goby_visualize_interfaces(YML_DIR DEPLOYMENT_YAML IMAGE_OUT PARAMETERS [FORMAT DOT|D2] DEPENDENCY1 DEPENDENCY2 ... )
# draws a deployment yaml file with Graphviz (FORMAT DOT, the default, rendered by dot) or
# D2 (FORMAT D2, rendered by d2, https://d2lang.com)
# YML_DIR: directory to YML interface files
# DEPLOYMENT_YAML: deployment file
# IMAGE_OUT: output image path; its extension (.pdf, .svg, .png) sets the image type
# parameters: extra parameters for goby_clang_tool
# DEPENDENCY1..N: YML dependencies (list of targets from GOBY_EXPORT_INTERFACE())
function(GOBY_VISUALIZE_INTERFACES YML_DIR DEPLOYMENT_YAML IMAGE_OUT PARAMETERS)
  cmake_parse_arguments(VIZ "" "FORMAT" "" ${ARGN})
  if(NOT VIZ_FORMAT)
    set(VIZ_FORMAT DOT)
  endif()
  string(TOLOWER ${VIZ_FORMAT} GRAPH_FORMAT)
  if(NOT GRAPH_FORMAT STREQUAL "dot" AND NOT GRAPH_FORMAT STREQUAL "d2")
    message(FATAL_ERROR "goby_visualize_interfaces: FORMAT must be DOT or D2, not ${VIZ_FORMAT}")
  endif()

  get_filename_component(ABS_IMAGE_OUT ${IMAGE_OUT} ABSOLUTE)
  get_filename_component(OUT_DIR ${ABS_IMAGE_OUT} DIRECTORY)
  get_filename_component(IMAGE_EXT ${ABS_IMAGE_OUT} EXT)

  # .pdf -> pdf
  string(SUBSTRING ${IMAGE_EXT} 1 -1 IMAGE_TYPE)
  
  get_filename_component(ABS_DEPLOYMENT_YAML ${DEPLOYMENT_YAML} ABSOLUTE)


  if(PARAMETERS)
    string(REGEX REPLACE "[ -]" "_" PARAMETERS_SUFFIX ${PARAMETERS})
  endif()
  
  file(MAKE_DIRECTORY ${OUT_DIR})
  get_filename_component(DEPLOYMENT_NAME_FROM_FILENAME ${ABS_DEPLOYMENT_YAML} NAME_WE)
  set(GRAPH_OUT_FILE "${DEPLOYMENT_NAME_FROM_FILENAME}${PARAMETERS_SUFFIX}.${GRAPH_FORMAT}")
  set(ABS_GRAPH_OUT "${OUT_DIR}/${GRAPH_OUT_FILE}")
  
  add_custom_command(
    OUTPUT ${ABS_GRAPH_OUT}
    COMMAND goby_clang_tool
    ARGS -viz -format=${GRAPH_FORMAT} -outdir ${OUT_DIR} -o ${GRAPH_OUT_FILE} -p ${CMAKE_BINARY_DIR} ${ABS_DEPLOYMENT_YAML} ${PARAMETERS}
    DEPENDS ${VIZ_UNPARSED_ARGUMENTS} ${ABS_DEPLOYMENT_YAML}
    WORKING_DIRECTORY ${YML_DIR}
    )

  set_source_files_properties(${ABS_GRAPH_OUT} PROPERTIES GENERATED TRUE)

  if(GRAPH_FORMAT STREQUAL "d2")
    add_custom_command(
      OUTPUT ${ABS_IMAGE_OUT}
      COMMAND d2
      ARGS ${ABS_GRAPH_OUT} ${ABS_IMAGE_OUT}
      DEPENDS ${ABS_GRAPH_OUT}
      )
  else()
    add_custom_command(
      OUTPUT ${ABS_IMAGE_OUT}
      COMMAND dot
      ARGS -T${IMAGE_TYPE} -o ${ABS_IMAGE_OUT} ${ABS_GRAPH_OUT}
      DEPENDS ${ABS_GRAPH_OUT}
      )
  endif()

  set_source_files_properties(${ABS_IMAGE_OUT} PROPERTIES GENERATED TRUE)
  
  add_custom_target(${DEPLOYMENT_NAME_FROM_FILENAME}${PARAMETERS_SUFFIX}_interface_viz ALL DEPENDS ${ABS_IMAGE_OUT})
endfunction()
