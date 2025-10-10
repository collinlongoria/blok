cmake_minimum_required(VERSION 3.20)
project(
  blok
  VERSION 0.1.0
  LANGUAGES C CXX
)

# ---- Output dirs ----
set(OUT_BIN ${CMAKE_SOURCE_DIR}/build/$<CONFIG>/bin)
set(OUT_LIB ${CMAKE_SOURCE_DIR}/build/$<CONFIG>/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUT_BIN})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUT_LIB})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUT_LIB})

# ---- Asset folder handler ----
set(ASSETS_SRC_DIR "${CMAKE_SOURCE_DIR}/assets")
set(ASSETS_DST_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets")
add_custom_target(refresh_assets ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${ASSETS_DST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_SRC_DIR}" "${ASSETS_DST_DIR}"
        COMMENT "Refreshing assets folder..."
)

# ---- Consistent MSVC/CUDA runtimes (fixes LNK4098) ----
if (MSVC)
    # Use dynamic MSVC runtime: /MD (Release) and /MDd (Debug)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
# Make CUDA use shared cudart to match /MD runtime
set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)

# ---- Subdirectories ----
add_subdirectory(external/glfw) # builds glfw3
add_subdirectory(external/dawn) # builds dawn and glfw3webgpu extension
add_subdirectory(external/shader-pipe)
add_subdirectory(blok)

# ---- Project-wide properties ----
add_dependencies(blok refresh_assets)
set_target_properties(blok PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
)



