add_library(Jade STATIC
  src/Core/Application.cpp
  src/Core/Log.cpp
  src/Platform/GLFW/GlfwRuntime.cpp
  src/Platform/GLFW/GlfwWindow.cpp
  src/Platform/Window.cpp
  src/Renderer/Renderer.cpp
  src/Renderer/OpenGL/OpenGLContext.cpp
  src/Renderer/OpenGL/OpenGLRenderer.cpp
  include/Jade/Jade.h
  include/Jade/Core/Application.h
  include/Jade/Core/Log.h
  include/Jade/Platform/Window.h
  include/Jade/Renderer/Renderer.h
  src/Platform/GLFW/GlfwRuntime.h
  src/Platform/GLFW/GlfwWindow.h
  src/Renderer/OpenGL/OpenGLContext.h
  src/Renderer/OpenGL/OpenGLRenderer.h
)
add_library(Jade::Jade ALIAS Jade)
target_include_directories(Jade PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(Jade PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(Jade PRIVATE glfw jade_glad OpenGL::GL)
jade_configure_target(Jade)
get_target_property(jade_sources Jade SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${jade_sources})
