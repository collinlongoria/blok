/*
* File: gpu_handles
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*
* Description: Type aliasing for GPU types
*/
#ifndef BLOK_GPU_HANDLES_HPP
#define BLOK_GPU_HANDLES_HPP

#include <cstdint>

namespace blok {
using BufferHandle = uint64_t;
using ImageHandle = uint64_t;
using ImageViewHandle = uint64_t;
using SamplerHandle = uint64_t;
using BindGroupLayoutHandle = uint64_t;
using BindGroupHandle = uint64_t;
using PipelineLayoutHandle = uint64_t;
using ShaderModuleHandle = uint64_t;
using GraphicsPipelineHandle = uint64_t;
using ComputePipelineHandle = uint64_t;
using CommandListHandle = uint64_t;
using FenceHandle = uint64_t;
using SemaphoreHandle = uint64_t;
using QueryPoolHandle = uint64_t;
using SwapchainHandle = uint64_t;
}

#endif //BLOK_GPU_HANDLES_HPP