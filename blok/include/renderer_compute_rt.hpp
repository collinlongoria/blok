/*
* File: renderer_compute_rt.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/17/2025
* Description: Compute shader-based voxel ray tracing using double DDA traversal
*/
#ifndef RENDERER_COMPUTE_RT_HPP
#define RENDERER_COMPUTE_RT_HPP

#include <array>
#include "resources.hpp"
#include "vulkan_context.hpp"

namespace blok {

class Renderer;

// 3D image to serve as the chunk lookup table (replaces TLAS)
struct ChunkIndexMap {
    vk::Image handle{};
    VmaAllocation alloc{};
    vk::ImageView view{};
    vk::Sampler sampler{};

    glm::ivec3 dimensions{0};  // Grid dimensions

    // Invalid chunk sentinel
    static constexpr uint32_t INVALID_CHUNK = 0xFFFFFFFFu;
};

// Per-chunk metadata for compute traversal
struct alignas(16) ChunkGpuCompute {
    uint32_t nodeOffset;        // Offset into global Sv64Node array
    uint32_t rootNodeIndex;     // Root node index (relative to nodeOffset)
    uint32_t nodeCount;         // Total nodes in this chunk
    uint32_t flags;             // CHUNK_FLAG_EMPTY, CHUNK_FLAG_SOLID, etc.

    glm::vec3 worldMin;         // Chunk world-space min corner
    float size;                 // Chunk size in world units

    glm::vec3 worldMax;         // Chunk world-space max corner
    float _pad;
};
static_assert(sizeof(ChunkGpuCompute) == 48, "ChunkGpuCompute size mismatch");

// Chunk flags
constexpr uint32_t CHUNK_FLAG_EMPTY = 0x1;  // Skip traversal entirely
constexpr uint32_t CHUNK_FLAG_SOLID = 0x2;  // Single material, fast path

// Chunk grid info UBO
struct alignas(16) ChunkGridUBO {
    glm::vec3 gridWorldMin;     // World-space origin of chunk grid
    float chunkSize;            // Size of each chunk in world units

    glm::ivec3 gridDimensions;  // Number of chunks in X, Y, Z
    float invChunkSize;         // 1.0f / chunkSize

    glm::vec3 gridWorldMax;     // gridWorldMin + gridDimensions * chunkSize
    float _pad;
};
static_assert(sizeof(ChunkGridUBO) == 48, "ChunkGridUBO size mismatch");

// Extended world data for compute-based ray tracing
struct WorldComputeGpu {
    // SV64 64-tree data
    std::vector<Sv64Node> globalNodes;
    std::vector<ChunkGpuCompute> chunks;

    Buffer sv64Buffer{};
    Buffer chunkBuffer{};

    // Materials
    std::vector<MaterialGpu> materials;
    Buffer materialBuffer{};

    // Chunk grid info
    ChunkGridUBO gridInfo{};
    Buffer gridInfoBuffer{};

    // 3D chunk index map (replaces TLAS)
    ChunkIndexMap chunkIndexMap{};

    // CPU-side chunk index data (filled by packChunksForCompute)
    // This is a flattened 3D array: index = x + y * dimX + z * dimX * dimY
    std::vector<uint32_t> chunkIndexData;

    // Grid offset (minimum chunk coordinates, used for coordinate translation)
    glm::ivec3 gridOffset{0};
};

struct ComputeRTPipeline {
    vk::Pipeline pipeline{};
    vk::PipelineLayout layout{};
};

class ComputeRT {
public:
    Renderer* r;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    vk::DescriptorSetLayout rtSetLayout{};
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> rtSets{};

    ComputeRTPipeline rtPipeline{};

public:
    explicit ComputeRT(Renderer* r);

    void createDescriptorSetLayout();
    void allocateDescriptorSet();
    void updateDescriptorSet(const WorldComputeGpu& world, uint32_t frameIndex);

    void createPipeline();

    void dispatchRayTracing(vk::CommandBuffer cmd, uint32_t w, uint32_t h, uint32_t frameIndex);

    void cleanup();
};

// Helper functions for world data management
void packChunksForCompute(const class ChunkManager& mgr, WorldComputeGpu& gpuWorld);

}

#endif // RENDERER_COMPUTE_RT_HPP