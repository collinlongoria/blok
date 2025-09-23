# ---- Sources ----
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
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
        CUDA_ARCHITECTURES 89
        CUDA_SEPARABLE_COMPILATION OFF
)

# CUDA usage requirements
target_include_directories(blok PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(blok PRIVATE ${CUDAToolkit_LIBRARIES})

# ---- WebGPU (dawn) ---
target_link_libraries(blok PRIVATE glfw3webgpu)

add_custom_command(TARGET blok POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:blok> $<TARGET_FILE_DIR:blok>
        COMMAND_EXPAND_LISTS
)

if (EXISTS "${TINT_EXE}")
    add_custom_command(TARGET blok POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TINT_EXE}" $<TARGET_FILE_DIR:blok>)
endif()

# (Dawn also needs directX shader compiler - working on fixing this)
# Collect all DLLs in external/directx
file(GLOB DIRECTX_DLLS "${CMAKE_SOURCE_DIR}/external/directx/*.dll")

# Copy them to the build directory
foreach(dll ${DIRECTX_DLLS})
    add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${dll}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endforeach()


# ---- GLM ----
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
        ${CMAKE_SOURCE_DIR}/external/glm/glm)
target_link_libraries(blok PUBLIC glm)

# ---- GLFW ----
#include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
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

# ---- Assets (Shaders, Textures, etc.) ------------------------------------------------
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)