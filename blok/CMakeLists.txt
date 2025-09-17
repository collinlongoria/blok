# ---- Sources ----
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/include/*.hpp
)

add_executable(blok ${SOURCES})

target_include_directories(blok
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---- CUDA ----
enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

# Pick an arch that matches your GPU:
#   86 = Ampere (RTX 30xx), 89 = Ada (RTX 40xx), 75 = Turing (RTX 20xx), 61 = Pascal, 52 = Maxwell
set_target_properties(blok PROPERTIES
        CUDA_ARCHITECTURES 86
        CUDA_SEPARABLE_COMPILATION OFF
)

# CUDA usage requirements
target_include_directories(blok PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(blok PRIVATE ${CUDAToolkit_LIBRARIES})

# ---- Dawn (WebGPU) ----
set(DAWN_ROOT ${CMAKE_SOURCE_DIR}/external/dawn)
target_include_directories(blok PRIVATE ${DAWN_ROOT}/include)

add_library(dawn SHARED IMPORTED)
set_target_properties(dawn PROPERTIES
  IMPORTED_LOCATION             "${DAWN_ROOT}/bin/webgpu_dawn.dll"
  IMPORTED_IMPLIB               "${DAWN_ROOT}/lib/webgpu_dawn.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${DAWN_ROOT}/include"
)
target_link_libraries(blok PRIVATE dawn)

add_custom_command(TARGET blok POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${DAWN_ROOT}/bin/webgpu_dawn.dll"
          $<TARGET_FILE_DIR:blok>
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DAWN_ROOT}/bin/tint.exe"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# ---- GLM ----
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
        ${CMAKE_SOURCE_DIR}/external/glm/glm)
target_link_libraries(blok PUBLIC glm)

# ---- GLFW ----
include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
target_link_libraries(blok PRIVATE glfw)

# ---- GLAD ----
add_library(glad STATIC
        ${CMAKE_SOURCE_DIR}/external/glad/src/glad.c
)
target_include_directories(glad PUBLIC
        ${CMAKE_SOURCE_DIR}/external/glad/include
)
target_link_libraries(blok PRIVATE glad)

# ---- OpenGL ----
find_package(OpenGL REQUIRED)
target_link_libraries(blok PRIVATE OpenGL::GL)
