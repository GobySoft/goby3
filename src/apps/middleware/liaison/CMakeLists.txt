#
# Ubuntu patched this compiler to hell
# gcc (Ubuntu/Linaro 4.4.4-14ubuntu5) 4.4.5
#
exec_program(${CMAKE_CXX_COMPILER}
  ARGS --version
  OUTPUT_VARIABLE GCC_COMPILER_VERSION
)

if(GCC_COMPILER_VERSION MATCHES ".*4\\.4\\.4\\-14ubuntu5.*")
  message(" ")
  message("    !!!!! WARNING Your compiler is BUGGY. !!!!! ")
  message(" ")
  message("    If possible, upgrade your compiler to e.g. g++ 4.5:")
  message(" ")
  message("       $ sudo apt-get install g++-4.5")
  message(" ")
  message("    And build using that compiler cmake -DCMAKE_CXX_COMPILER=g++-4.5")
  message(" ")
  message("    we will now disable all assertions as a work around, by")
  message("    building using -DNDEBUG.")
  message(" ")
  add_definitions(-DNDEBUG)
endif(GCC_COMPILER_VERSION MATCHES ".*4\\.4\\.4\\-14ubuntu5.*")


add_definitions(-DGOBY_LIAISON_COMPILED_DOCROOT="${goby_SHARE_DIR}/liaison")
add_definitions(-DGOBY_LIAISON_INSTALLED_DOCROOT="${CMAKE_INSTALL_PREFIX}/share/goby/liaison")


#file(GLOB PROTOS *.proto)
#protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTOS})

add_executable(goby_liaison
  liaison.cpp
  liaison_home.cpp  
  liaison_wt_thread.cpp)
#  ${PROTO_SRCS} ${PROTO_HDRS})

goby_find_required_package(Boost 1.40.0 COMPONENTS signals)

target_link_libraries(goby_liaison
  ${Boost_LIBRARIES}
  ${Wt_LIBRARIES}
  pthread
  goby_common
  goby_util
  goby_middleware
  )
