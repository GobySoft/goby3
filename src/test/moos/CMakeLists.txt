get_filename_component(dccl_examples_dir ./ ABSOLUTE)
add_definitions(-DDCCL_EXAMPLES_DIR="${dccl_examples_dir}")

add_subdirectory(translator1)
add_subdirectory(goby_app_config)
