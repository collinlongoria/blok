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
        CUDA_ARCHITECTURES 86
        CUDA_SEPARABLE_COMPILATION OFF
)

# CUDA usage requirements
target_include_directories(blok PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(blok PRIVATE ${CUDAToolkit_LIBRARIES})

target_compile_options(blok PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)

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


file(GLOB DIRECTX_DLLS "${CMAKE_SOURCE_DIR}/external/directx/*.dll")

if(WIN32)
    # Copy them to the build directory
    foreach(dll ${DIRECTX_DLLS})
        add_custom_command(
                TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endforeach()
endif()

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

# ---- ImGui ----
add_library(imgui STATIC
        ${CMAKE_SOURCE_DIR}/external/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/external/imgui/imgui_demo.cpp
        ${CMAKE_SOURCE_DIR}/external/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/external/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/external/imgui/imgui_widgets.cpp

        ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_glfw.cpp
        ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PRIVATE
        ${CMAKE_SOURCE_DIR}/external/imgui
        ${CMAKE_SOURCE_DIR}/external/imgui/backends
        ${CMAKE_SOURCE_DIR}/external/glfw/include
)
target_include_directories(blok PUBLIC 
        ${CMAKE_SOURCE_DIR}/external/imgui
        ${CMAKE_SOURCE_DIR}/external/imgui/backends
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_link_libraries(blok PRIVATE imgui)

# ---- Shader Pipeline ----
target_link_libraries(blok PRIVATE shaderpipe)

# ---- Assets (Shaders, Textures, etc.) ------------------------------------------------
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)

