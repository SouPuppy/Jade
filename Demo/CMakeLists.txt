add_executable(JadeDemo src/main.cpp)
target_link_libraries(JadeDemo PRIVATE Jade::Jade)
jade_configure_target(JadeDemo)
jade_deploy_runtime(JadeDemo)
# Run even when the executable does not relink, so edited assets are deployed too.
add_custom_target(JadeDemoAssets
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:JadeDemo>/assets"
  VERBATIM)
add_dependencies(JadeDemo JadeDemoAssets)
