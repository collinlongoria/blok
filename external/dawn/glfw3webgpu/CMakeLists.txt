# The glfw3webgpu target
add_library(glfw3webgpu STATIC glfw3webgpu.c)
target_include_directories(glfw3webgpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(glfw3webgpu PUBLIC glfw webgpu_dawn)
