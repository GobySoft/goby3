unset(GOBY_FRONTSEAT_PLUGIN_TARGET_LIST CACHE)
unset(GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST CACHE)

function(goby_register_frontseat_plugin target shortname)
  list(APPEND GOBY_FRONTSEAT_PLUGIN_TARGET_LIST ${target})
  list(APPEND GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST ${shortname})
  set(GOBY_FRONTSEAT_PLUGIN_TARGET_LIST ${GOBY_FRONTSEAT_PLUGIN_TARGET_LIST} CACHE INTERNAL "GOBY_FRONTSEAT_PLUGIN_TARGET_LIST")
  set(GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST ${GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST} CACHE INTERNAL "GOBY_FRONTSEAT_PLUGIN_SHORTNAME_LIST")
endfunction()

add_subdirectory(bluefin)
add_subdirectory(waveglider)
add_subdirectory(iver)
add_subdirectory(simulator)
