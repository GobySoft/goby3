add_definitions(-DGOBY_LIAISON_COMPILED_DOCROOT="${goby_SHARE_DIR}/goby/liaison")
add_definitions(-DGOBY_LIAISON_INSTALLED_DOCROOT="${CMAKE_INSTALL_PREFIX}/share/goby/liaison")

add_library(goby_liaison_core SHARED
  liaison_home.cpp  
  liaison_wt_thread.cpp
  liaison_commander.cpp
  liaison_scope.cpp
)

target_link_libraries(goby_liaison_core
  goby
  goby_zeromq
  ${Wt_LIBRARIES}
  )

add_executable(goby_liaison
  liaison.cpp
  )

add_executable(goby_liaison_standalone
  liaison.cpp
  )

target_compile_definitions(goby_liaison_standalone
  PRIVATE LIAISON_STANDALONE=1
  )

generate_middleware_interfaces(goby_liaison)

set(LIB
  pthread
  goby_liaison_core
)

target_link_libraries(goby_liaison
  ${LIB}
  )

target_link_libraries(goby_liaison_standalone
  ${LIB}
  )
