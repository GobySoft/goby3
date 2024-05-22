if(enable_llvm)
                      
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

  add_executable(goby_clang_tool tool.cpp generate.cpp visualize.cpp)

  execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags OUTPUT_VARIABLE LLVM_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)

  # llvm-18 requires C++17
  if(${LLVM_CONFIG_VERSION} VERSION_GREATER_EQUAL 14.0.0)
    set(clang_libs
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
      clangAPINotes
      clangBasic)
  else()
    set(clang_libs
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
      clangBasic)    
  endif()

  
  set_target_properties(goby_clang_tool PROPERTIES COMPILE_FLAGS "${LLVM_DEFINITIONS} -std=c++17" LINK_FLAGS "${LLVM_LD_FLAGS}" )

  target_link_libraries(goby_clang_tool
    PRIVATE
    ${clang_libs}
    LLVM
    goby
    yaml-cpp
    )


endif()
