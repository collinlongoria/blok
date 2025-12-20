/*
* File: renderer_compute_rt.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/17/2025
* Description: Compute shader-based voxel ray tracing implementation
*/
#include "renderer_compute_rt.hpp"

#include <iostream>
#include <cstring>

#include "renderer.hpp"
#include "chunk_manager.hpp"

namespace blok {

// ============================================================================
// ComputeRT Implementation
// ============================================================================

ComputeRT::ComputeRT(Renderer* r_)
    : r(r_) {}

void ComputeRT::createDescriptorSetLayout() {
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        // binding 0: Frame UBO
        {0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 1: Chunk index map (3D texture)
        {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 2: Chunk grid UBO
        {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 3: SV64 nodes buffer
        {3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 4: Chunk metadata buffer
        {4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 5: Materials buffer
        {5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 6: Output color image
        {6, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 7: World position output
        {7, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 8: Normal/roughness output
        {8, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 9: Albedo/metallic output
        {9, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // binding 10: Motion vectors output
        {10, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo ci{};
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();

    rtSetLayout = r->m_device.createDescriptorSetLayout(ci);
}

void ComputeRT::allocateDescriptorSet() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rtSets[i] = r->m_descAlloc.allocate(r->m_device, rtSetLayout);
    }
}

void ComputeRT::updateDescriptorSet(const WorldComputeGpu& world, uint32_t frameIndex) {
    vk::DescriptorSet currentSet = rtSets[frameIndex];

    // Frame UBO (binding 0)
    vk::DescriptorBufferInfo frameInfo{};
    frameInfo.buffer = r->m_frames[frameIndex].frameUBO.handle;
    frameInfo.offset = 0;
    frameInfo.range = sizeof(FrameUBO);

    vk::WriteDescriptorSet frameWrite{};
    frameWrite.dstSet = currentSet;
    frameWrite.dstBinding = 0;
    frameWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    frameWrite.descriptorCount = 1;
    frameWrite.pBufferInfo = &frameInfo;

    // Chunk index map (binding 1)
    vk::DescriptorImageInfo chunkMapInfo{};
    chunkMapInfo.sampler = world.chunkIndexMap.sampler;
    chunkMapInfo.imageView = world.chunkIndexMap.view;
    chunkMapInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet chunkMapWrite{};
    chunkMapWrite.dstSet = currentSet;
    chunkMapWrite.dstBinding = 1;
    chunkMapWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    chunkMapWrite.descriptorCount = 1;
    chunkMapWrite.pImageInfo = &chunkMapInfo;

    // Chunk grid UBO (binding 2)
    vk::DescriptorBufferInfo gridInfo{};
    gridInfo.buffer = world.gridInfoBuffer.handle;
    gridInfo.offset = 0;
    gridInfo.range = sizeof(ChunkGridUBO);

    vk::WriteDescriptorSet gridWrite{};
    gridWrite.dstSet = currentSet;
    gridWrite.dstBinding = 2;
    gridWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    gridWrite.descriptorCount = 1;
    gridWrite.pBufferInfo = &gridInfo;

    // SV64 nodes buffer (binding 3)
    vk::DescriptorBufferInfo sv64Info{};
    sv64Info.buffer = world.sv64Buffer.handle;
    sv64Info.offset = 0;
    sv64Info.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet sv64Write{};
    sv64Write.dstSet = currentSet;
    sv64Write.dstBinding = 3;
    sv64Write.descriptorType = vk::DescriptorType::eStorageBuffer;
    sv64Write.descriptorCount = 1;
    sv64Write.pBufferInfo = &sv64Info;

    // Chunk metadata buffer (binding 4)
    vk::DescriptorBufferInfo chunkInfo{};
    chunkInfo.buffer = world.chunkBuffer.handle;
    chunkInfo.offset = 0;
    chunkInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet chunkWrite{};
    chunkWrite.dstSet = currentSet;
    chunkWrite.dstBinding = 4;
    chunkWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    chunkWrite.descriptorCount = 1;
    chunkWrite.pBufferInfo = &chunkInfo;

    // Materials buffer (binding 5)
    vk::DescriptorBufferInfo materialInfo{};
    materialInfo.buffer = world.materialBuffer.handle;
    materialInfo.offset = 0;
    materialInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet materialWrite{};
    materialWrite.dstSet = currentSet;
    materialWrite.dstBinding = 5;
    materialWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    materialWrite.descriptorCount = 1;
    materialWrite.pBufferInfo = &materialInfo;

    // Output color image (binding 6)
    vk::DescriptorImageInfo colorInfo{};
    colorInfo.imageView = r->m_denoiser.gbuffer.color.view;
    colorInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet colorWrite{};
    colorWrite.dstSet = currentSet;
    colorWrite.dstBinding = 6;
    colorWrite.descriptorType = vk::DescriptorType::eStorageImage;
    colorWrite.descriptorCount = 1;
    colorWrite.pImageInfo = &colorInfo;

    // World position output (binding 7)
    vk::DescriptorImageInfo wpInfo{};
    wpInfo.imageView = r->m_denoiser.gbuffer.worldPosition.view;
    wpInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet wpWrite{};
    wpWrite.dstSet = currentSet;
    wpWrite.dstBinding = 7;
    wpWrite.descriptorType = vk::DescriptorType::eStorageImage;
    wpWrite.descriptorCount = 1;
    wpWrite.pImageInfo = &wpInfo;

    // Normal/roughness output (binding 8)
    vk::DescriptorImageInfo nrInfo{};
    nrInfo.imageView = r->m_denoiser.gbuffer.normalRoughness.view;
    nrInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet nrWrite{};
    nrWrite.dstSet = currentSet;
    nrWrite.dstBinding = 8;
    nrWrite.descriptorType = vk::DescriptorType::eStorageImage;
    nrWrite.descriptorCount = 1;
    nrWrite.pImageInfo = &nrInfo;

    // Albedo/metallic output (binding 9)
    vk::DescriptorImageInfo amInfo{};
    amInfo.imageView = r->m_denoiser.gbuffer.albedoMetallic.view;
    amInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet amWrite{};
    amWrite.dstSet = currentSet;
    amWrite.dstBinding = 9;
    amWrite.descriptorType = vk::DescriptorType::eStorageImage;
    amWrite.descriptorCount = 1;
    amWrite.pImageInfo = &amInfo;

    // Motion vectors output (binding 10)
    vk::DescriptorImageInfo motionInfo{};
    motionInfo.imageView = r->m_denoiser.gbuffer.motionVectors.view;
    motionInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet motionWrite{};
    motionWrite.dstSet = currentSet;
    motionWrite.dstBinding = 10;
    motionWrite.descriptorType = vk::DescriptorType::eStorageImage;
    motionWrite.descriptorCount = 1;
    motionWrite.pImageInfo = &motionInfo;

    std::array<vk::WriteDescriptorSet, 11> writes = {
        frameWrite, chunkMapWrite, gridWrite, sv64Write, chunkWrite,
        materialWrite, colorWrite, wpWrite, nrWrite, amWrite, motionWrite
    };

    r->m_device.updateDescriptorSets(writes, {});
}

void ComputeRT::createPipeline() {
    // Load compute shader
    auto shaderModule = r->m_shaderManager.loadModule(
        "assets/shaders/voxel_raytrace.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    // Pipeline layout
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &rtSetLayout;

    rtPipeline.layout = r->m_device.createPipelineLayout(layoutInfo);

    // Create compute pipeline
    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = rtPipeline.layout;

    auto result = r->m_device.createComputePipeline(nullptr, pipelineInfo);
    rtPipeline.pipeline = result.value;

    r->m_device.destroyShaderModule(shaderModule.module);

    std::cout << "Compute ray tracing pipeline created\n";
}

void ComputeRT::dispatchRayTracing(vk::CommandBuffer cmd, uint32_t w, uint32_t h, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, rtPipeline.pipeline);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        rtPipeline.layout,
        0, rtSets[frameIndex], {}
    );

    // Dispatch one thread per pixel, workgroup size is 8x8
    uint32_t groupsX = (w + 7) / 8;
    uint32_t groupsY = (h + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

void ComputeRT::cleanup() {
    auto& device = r->m_device;

    if (rtPipeline.pipeline) {
        device.destroyPipeline(rtPipeline.pipeline);
        rtPipeline.pipeline = nullptr;
    }
    if (rtPipeline.layout) {
        device.destroyPipelineLayout(rtPipeline.layout);
        rtPipeline.layout = nullptr;
    }
    if (rtSetLayout) {
        device.destroyDescriptorSetLayout(rtSetLayout);
        rtSetLayout = nullptr;
    }
}

// ============================================================================
// World Data Packing for Compute
// ============================================================================

void packChunksForCompute(const ChunkManager& mgr, WorldComputeGpu& gpuWorld) {
    gpuWorld.globalNodes.clear();
    gpuWorld.chunks.clear();
    gpuWorld.chunkIndexData.clear();

    if (mgr.chunks.empty()) {
        return;
    }

    // Calculate grid bounds
    int32_t minCx = INT32_MAX, minCy = INT32_MAX, minCz = INT32_MAX;
    int32_t maxCx = INT32_MIN, maxCy = INT32_MIN, maxCz = INT32_MIN;

    for (const auto& kv : mgr.chunks) {
        const Chunk* ch = kv.second;
        minCx = std::min(minCx, ch->cx);
        minCy = std::min(minCy, ch->cy);
        minCz = std::min(minCz, ch->cz);
        maxCx = std::max(maxCx, ch->cx);
        maxCy = std::max(maxCy, ch->cy);
        maxCz = std::max(maxCz, ch->cz);
    }

    // Grid dimensions (add 1 because max is inclusive)
    glm::ivec3 gridDims = glm::ivec3(
        maxCx - minCx + 1,
        maxCy - minCy + 1,
        maxCz - minCz + 1
    );

    // Store grid offset for coordinate translation
    gpuWorld.gridOffset = glm::ivec3(minCx, minCy, minCz);

    // Calculate world-space bounds
    float chunkWorldSize = static_cast<float>(mgr.C) * mgr.voxelSize;
    glm::vec3 gridWorldMin = glm::vec3(
        static_cast<float>(minCx * static_cast<int32_t>(mgr.C)) * mgr.voxelSize,
        static_cast<float>(minCy * static_cast<int32_t>(mgr.C)) * mgr.voxelSize,
        static_cast<float>(minCz * static_cast<int32_t>(mgr.C)) * mgr.voxelSize
    );
    glm::vec3 gridWorldMax = gridWorldMin + glm::vec3(gridDims) * chunkWorldSize;

    // Fill in grid info
    gpuWorld.gridInfo.gridWorldMin = gridWorldMin;
    gpuWorld.gridInfo.chunkSize = chunkWorldSize;
    gpuWorld.gridInfo.gridDimensions = gridDims;
    gpuWorld.gridInfo.invChunkSize = 1.0f / chunkWorldSize;
    gpuWorld.gridInfo.gridWorldMax = gridWorldMax;

    // Initialize chunk index map with INVALID_CHUNK
    size_t mapSize = static_cast<size_t>(gridDims.x) * gridDims.y * gridDims.z;
    gpuWorld.chunkIndexData.resize(mapSize, ChunkIndexMap::INVALID_CHUNK);

    // Reserve space
    gpuWorld.globalNodes.reserve(1024);
    gpuWorld.chunks.reserve(mgr.chunks.size());

    uint32_t nodeOffset = 0;
    uint32_t chunkIndex = 0;

    for (const auto& kv : mgr.chunks) {
        const Chunk* ch = kv.second;
        const auto& nodes = ch->sv64.nodes;

        // Skip empty chunks (no nodes)
        if (nodes.empty()) {
            continue;
        }

        // Calculate grid position (relative to grid origin)
        int32_t gx = ch->cx - minCx;
        int32_t gy = ch->cy - minCy;
        int32_t gz = ch->cz - minCz;

        // Linear index in 3D array: x + y * dimX + z * dimX * dimY
        size_t linearIdx = static_cast<size_t>(gx) +
                          static_cast<size_t>(gy) * gridDims.x +
                          static_cast<size_t>(gz) * gridDims.x * gridDims.y;

        // Store chunk index in the map
        gpuWorld.chunkIndexData[linearIdx] = chunkIndex;

        // Calculate world-space bounds for this chunk
        glm::vec3 chunkWorldMin = glm::vec3(
            static_cast<float>(ch->cx * static_cast<int32_t>(mgr.C)) * mgr.voxelSize,
            static_cast<float>(ch->cy * static_cast<int32_t>(mgr.C)) * mgr.voxelSize,
            static_cast<float>(ch->cz * static_cast<int32_t>(mgr.C)) * mgr.voxelSize
        );
        glm::vec3 chunkWorldMax = chunkWorldMin + glm::vec3(chunkWorldSize);

        // Determine chunk flags
        uint32_t flags = 0;
        // Check if chunk is a single solid voxel covering the entire space
        if (nodes.size() == 1 && nodes[0].childMask == 0 && nodes[0].occupancy > 0.0f) {
            flags |= CHUNK_FLAG_SOLID;
        }

        // Create chunk metadata
        ChunkGpuCompute chunkGpu{};
        chunkGpu.nodeOffset = nodeOffset;
        chunkGpu.rootNodeIndex = 0;  // Root is always at index 0 within chunk's nodes
        chunkGpu.nodeCount = static_cast<uint32_t>(nodes.size());
        chunkGpu.flags = flags;
        chunkGpu.worldMin = chunkWorldMin;
        chunkGpu.size = chunkWorldSize;
        chunkGpu.worldMax = chunkWorldMax;

        gpuWorld.chunks.push_back(chunkGpu);

        // Append nodes to global array
        gpuWorld.globalNodes.insert(gpuWorld.globalNodes.end(), nodes.begin(), nodes.end());
        nodeOffset += static_cast<uint32_t>(nodes.size());

        chunkIndex++;
    }

    std::cout << "Compute RT packing: " << gpuWorld.chunks.size() << " chunks, "
              << gpuWorld.globalNodes.size() << " nodes\n";
    std::cout << "Grid dimensions: " << gridDims.x << "x" << gridDims.y << "x" << gridDims.z
              << " (" << mapSize << " cells)\n";
    std::cout << "Grid offset: (" << minCx << ", " << minCy << ", " << minCz << ")\n";
}

} // namespace blok