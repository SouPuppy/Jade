add_executable(JadeDemo src/main.cpp)
target_link_libraries(JadeDemo PRIVATE Jade::Jade)
jade_configure_target(JadeDemo)
jade_deploy_runtime(JadeDemo)
