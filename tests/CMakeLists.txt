add_executable(JadeSmokeTests
  SmokeTests.cpp
  MathTests.cpp
  GeometryTests.cpp
  MeshTests.cpp
  RendererTests.cpp
  InputTests.cpp
)
target_link_libraries(JadeSmokeTests PRIVATE Jade::Jade GTest::gtest_main jade_glad glfw)
# Only tests inspect pixels and scheduling internals; consumers receive public headers.
target_include_directories(JadeSmokeTests PRIVATE "${PROJECT_SOURCE_DIR}/Jade/src")
target_compile_definitions(JadeSmokeTests PRIVATE GLFW_INCLUDE_NONE
  JADE_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
jade_configure_target(JadeSmokeTests)
jade_deploy_runtime(JadeSmokeTests)
