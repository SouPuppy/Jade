add_executable(JadeSmokeTests
  PipelineTests.cpp
  SpriteAnimationTests.cpp
  FieldTests.cpp
  JobTests.cpp
  JobPhysicsTests.cpp
  SmokeTests.cpp
  MathTests.cpp
  CoreTests.cpp
  ComponentTests.cpp
  ArchetypeTests.cpp
  PhysicsTests.cpp
  CollisionTests.cpp
  MeshTests.cpp
  RendererTests.cpp
  ComputeRayTracerTests.cpp

  RenderModeTests.cpp
  SahTests.cpp
  InputTests.cpp
  LogTests.cpp
  FrameDiagnosticsTests.cpp
  EventLogTests.cpp
  SceneTests.cpp
  ModuleTests.cpp
  TestScene.hpp
)
target_link_libraries(JadeSmokeTests PRIVATE Jade::Jade GTest::gtest_main jade_glad glfw)
# Only tests inspect pixels and scheduling internals; consumers receive public headers.
target_include_directories(JadeSmokeTests PRIVATE "${PROJECT_SOURCE_DIR}/Jade/src")
target_compile_definitions(JadeSmokeTests PRIVATE GLFW_INCLUDE_NONE
  JADE_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
jade_configure_target(JadeSmokeTests)
jade_deploy_runtime(JadeSmokeTests)
if(JADE_BUILD_DEMO)
  target_sources(JadeSmokeTests PRIVATE UntitledTests.cpp)
  target_link_libraries(JadeSmokeTests PRIVATE UntitledGameplay)
  target_compile_definitions(JadeSmokeTests PRIVATE
    JADE_UNTITLED_ASSET_DIR="${PROJECT_SOURCE_DIR}/Demo/Untitled/Assets"
    JADE_VALIDATION_DIR="${PROJECT_BINARY_DIR}/validation")
endif()
