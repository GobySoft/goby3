get_filename_component(dccl_examples_dir ./ ABSOLUTE)
add_definitions(-DDCCL_EXAMPLES_DIR="${dccl_examples_dir}")

# Does not work with UBSan due to bug in boost::format
# See https://svn.boost.org/trac10/ticket/11632
if(NOT SANITIZE_UNDEFINED)
  add_subdirectory(translator1)
endif()
  
add_subdirectory(goby_app_config)
