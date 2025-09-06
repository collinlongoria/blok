set(SOURCES
        # Add all files under here:
        src/main.cpp
)

add_executable(blok ${SOURCES})

target_include_directories(blok
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Dawn (WebGPU)

# Path to Dawn
set(DAWN_ROOT ${CMAKE_SOURCE_DIR}/external/dawn)

#Header files
include_directories(${DAWN_ROOT}/include)

# Imported Dawn library
add_library(dawn SHARED IMPORTED)

set_target_properties(dawn PROPERTIES
        IMPORTED_LOCATION "${DAWN_ROOT}/bin/webgpu_dawn.dll"
        IMPORTED_IMPLIB "${DAWN_ROOT}/lib/webgpu_dawn.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${DAWN_ROOT}/include"
)

target_link_libraries(blok PRIVATE dawn)

# TODO: move all of this into it's own CMake list
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DAWN_ROOT}/bin/webgpu_dawn.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# (also copying tint.exe, for when it is needed.
# I will consider better ways in the future
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DAWN_ROOT}/bin/tint.exe"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# GLM (Header-only interface)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_SOURCE_DIR}/external/glm/glm)
target_link_libraries(blok PUBLIC glm)

# GLFW (built outside of here)
include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
target_link_libraries(blok PRIVATE glfw)