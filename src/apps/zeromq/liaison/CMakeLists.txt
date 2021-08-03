add_definitions(-DGOBY_LIAISON_COMPILED_DOCROOT="${goby_SHARE_DIR}/goby/liaison")
add_definitions(-DGOBY_LIAISON_INSTALLED_DOCROOT="${CMAKE_INSTALL_PREFIX}/share/goby/liaison")

add_executable(goby_liaison
  liaison.cpp
  liaison_home.cpp  
  liaison_wt_thread.cpp
  liaison_commander.cpp
  liaison_scope.cpp
  )

generate_middleware_interfaces(goby_liaison)

target_link_libraries(goby_liaison
  "-rdynamic"
  ${Boost_LIBRARIES}
  ${Wt_LIBRARIES}
  pthread
  goby
  goby_zeromq
  )
