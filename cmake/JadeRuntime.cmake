set(JADE_MESA_RUNTIME_DIR "${PROJECT_SOURCE_DIR}/runtime/mesa/x64" CACHE PATH
  "Directory containing the Windows Mesa runtime DLLs")
set(_jade_software_rendering_default OFF)
if(WIN32 AND EXISTS "${JADE_MESA_RUNTIME_DIR}/opengl32.dll")
  set(_jade_software_rendering_default ON)
endif()
option(JADE_USE_SOFTWARE_RENDERING "Deploy Mesa and use llvmpipe"
  ${_jade_software_rendering_default})

if(JADE_USE_SOFTWARE_RENDERING)
  if(NOT WIN32)
    message(FATAL_ERROR "JADE_USE_SOFTWARE_RENDERING deploys Windows Mesa DLLs only")
  endif()
  foreach(dll IN ITEMS opengl32.dll libgallium_wgl.dll)
    if(NOT EXISTS "${JADE_MESA_RUNTIME_DIR}/${dll}")
      message(FATAL_ERROR "Missing Mesa runtime: ${JADE_MESA_RUNTIME_DIR}/${dll}")
    endif()
  endforeach()
endif()

function(jade_deploy_runtime target)
  if(NOT WIN32)
    return()
  endif()
  # Always run, including when toggling Mesa does not cause a relink.
  add_custom_target(${target}Runtime
    COMMAND "${CMAKE_COMMAND}"
      "-DJADE_OUTPUT_DIR=$<TARGET_FILE_DIR:${target}>"
      "-DJADE_USE_SOFTWARE_RENDERING=${JADE_USE_SOFTWARE_RENDERING}"
      "-DJADE_MESA_RUNTIME_DIR=${JADE_MESA_RUNTIME_DIR}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DeployWindowsRuntime.cmake"
    VERBATIM)
  add_dependencies(${target} ${target}Runtime)
  set_property(TARGET ${target} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
    "$<TARGET_FILE_DIR:${target}>")
  if(JADE_USE_SOFTWARE_RENDERING)
    set_property(TARGET ${target} PROPERTY VS_DEBUGGER_ENVIRONMENT
      "GALLIUM_DRIVER=llvmpipe")
  endif()
endfunction()
