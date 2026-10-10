# Set before project() so C, C++, GLFW and GLAD all use the same CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(jade_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
