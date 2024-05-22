if(enable_llvm)
                      
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

  add_executable(goby_clang_tool tool.cpp generate.cpp visualize.cpp)

  execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags OUTPUT_VARIABLE LLVM_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)

  # llvm-18 requires C++17
  set_target_properties(goby_clang_tool PROPERTIES COMPILE_FLAGS "${LLVM_DEFINITIONS} -std=c++17" LINK_FLAGS "${LLVM_LD_FLAGS}" )

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
    clangAPINotes
    clangSupport
    clangBasic
    LLVM
    goby
    yaml-cpp
    )


endif()
