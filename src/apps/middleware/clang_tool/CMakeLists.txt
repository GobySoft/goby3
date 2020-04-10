include_directories(${LLVM_INCLUDE_DIRS})

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
