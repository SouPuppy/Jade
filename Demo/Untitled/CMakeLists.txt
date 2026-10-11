add_library(UntitledGameplay STATIC
  Source/Game.cpp Source/Game.hpp
  Source/PlayerController.cpp Source/PlayerController.hpp
  Source/CameraController.cpp Source/CameraController.hpp
  Source/World.cpp Source/World.hpp)
target_link_libraries(UntitledGameplay PUBLIC Jade::Jade)
target_include_directories(UntitledGameplay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
jade_configure_target(UntitledGameplay)
add_executable(Untitled Source/main.cpp)
target_link_libraries(Untitled PRIVATE UntitledGameplay)
jade_configure_target(Untitled)
jade_deploy_runtime(Untitled)
add_custom_target(UntitledAssets
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:Untitled>/UntitledAssets"
  VERBATIM)
add_dependencies(Untitled UntitledAssets)
