find_program(LLVM_CONFIG_EXECUTABLE "llvm-config")

set(BUILD_GOBY_CLANG_TOOL OFF)	

if(NOT LLVM_CONFIG_EXECUTABLE)
  message(STATUS "llvm-config NOT found. Will not attempt to build goby_clang_tool. (To install run 'apt install llvm')")
else()  
  execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --version OUTPUT_VARIABLE LLVM_CONFIG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(${LLVM_CONFIG_VERSION} VERSION_LESS 6.0.0)
    message(STATUS "llvm-config is too old (must be version 6+). Searching for newer llvm-config")
    unset(LLVM_CONFIG_EXECUTABLE CACHE)
    find_program(LLVM_CONFIG_EXECUTABLE NAMES "llvm-config-7" "llvm-config-8" "llvm-config-9")
  endif()

  if(NOT LLVM_CONFIG_EXECUTABLE)
    message(STATUS "llvm-config-7, llvm-config-8, or llvm-config-9 NOT found. Will not attempt to build goby_clang_tool. (To install run 'apt install llvm')")
  else()
    message(STATUS "Using ${LLVM_CONFIG_EXECUTABLE} to find LLVM/Clang libraries:")
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir OUTPUT_VARIABLE LLVM_CMAKE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "\tUsing LLVM CMake path: ${LLVM_CMAKE_DIR}")
    find_package(LLVM REQUIRED CONFIG PATHS ${LLVM_CMAKE_DIR} NO_DEFAULT_PATH)
    if(LLVM_FOUND)
      message(STATUS "\tFound LLVM version ${LLVM_PACKAGE_VERSION}: ${LLVM_INCLUDE_DIR}")
      
      find_file(CLANG_TOOLING_INCLUDE Tooling.h PATHS ${LLVM_INCLUDE_DIR}/clang/Tooling NO_DEFAULT_PATH)
      if(CLANG_TOOLING_INCLUDE)
        find_package(yaml-cpp QUIET)
        if(YAML_CPP_LIBRARIES)
          set(BUILD_GOBY_CLANG_TOOL ON)		
        else()
          message(STATUS "Could NOT find YAML-CPP. Will not attempt to build goby_clang_tool. (To install run 'apt install libyaml-cpp-dev')")      
        endif()
      else()
	message(STATUS "Could NOT find libclang. Will not attempt to build goby_clang_tool. (To install run 'apt install libclang-dev')")      
      endif()	

    else()
      message(STATUS "Could NOT find LLVM. Will not attempt to build goby_clang_tool")
    endif()
  endif()
endif()

if(BUILD_GOBY_CLANG_TOOL)
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

  add_executable(goby_clang_tool tool.cpp generate.cpp visualize.cpp)

  execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags OUTPUT_VARIABLE LLVM_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  set_target_properties(goby_clang_tool PROPERTIES COMPILE_FLAGS "${LLVM_DEFINITIONS}" LINK_FLAGS "${LLVM_LD_FLAGS}" )


  target_link_libraries(goby_clang_tool
    PRIVATE
    clangTooling
    clangFrontend
    clangDriver
    clangSerialization
    clangParse
    clangSema
    clangAnalysis
    clangEdit
    clangAST
    clangASTMatchers
    clangLex
    clangBasic
    LLVM
    goby
    yaml-cpp
    )

endif()
