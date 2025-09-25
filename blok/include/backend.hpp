/*
* File: backend
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: separates backends
*/
#pragma once
namespace blok {
enum class RenderBackend {
    OpenGL,
    WEBGPU_VULKAN,
    WEBGPU_D3D12, // windows only
    CUDA
};

}
