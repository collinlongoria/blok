# ---- Dawn (WebGPU) ----
set(DAWN_ROOT ${CMAKE_SOURCE_DIR}/external/dawn)
set(DAWN_DEBUG=1)

add_library(webgpu_dawn SHARED IMPORTED GLOBAL)
set_target_properties(webgpu_dawn PROPERTIES
        IMPORTED_LOCATION             "${DAWN_ROOT}/bin/webgpu_dawn.dll"
        IMPORTED_IMPLIB               "${DAWN_ROOT}/lib/webgpu_dawn.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${DAWN_ROOT}/include"
)

add_subdirectory(glfw3webgpu)