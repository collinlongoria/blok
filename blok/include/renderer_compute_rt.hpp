/*
* File: renderer_compute_rt.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/17/2025
*/
#ifndef RENDERER_COMPUTE_RT_HPP
#define RENDERER_COMPUTE_RT_HPP

#include <array>
#include "resources.hpp"
#include "vulkan_context.hpp"

namespace blok {

class Renderer;

// 3D image to serve as the chunk lookup table
struct ChunkIndexMap {
    vk::Image handle{};
    VmaAllocation alloc{};
    vk::ImageView view{};
    vk::Sampler sampler{};

    glm::ivec3 dimensions{0};

    static constexpr uint32_t INVALID_CHUNK = 0xFFFFFFFFu;
};

struct alignas(16) ChunkGpuCompute {
    uint32_t nodeOffset;
    uint32_t rootNodeIndex;
    uint32_t nodeCount;
    uint32_t flags;

    glm::vec3 worldMin;
    float size;

    glm::vec3 worldMax;
    float _pad;
};
static_assert(sizeof(ChunkGpuCompute) == 48, "ChunkGpuCompute size mismatch");

constexpr uint32_t CHUNK_FLAG_EMPTY = 0x1; // skip traversal entirely
constexpr uint32_t CHUNK_FLAG_SOLID = 0x2; // single material, fast path

struct alignas(16) ChunkGridUBO {
    glm::vec3 gridWorldMin;
    float chunkSize;

    glm::ivec3 gridDimensions;
    float invChunkSize;

    glm::vec3 gridWorldMax;
    float _pad;
};
static_assert(sizeof(ChunkGridUBO) == 48, "ChunkGridUBO size mismatch");

struct WorldComputeGpu {
    std::vector<Sv64Node> globalNodes;
    std::vector<ChunkGpuCompute> chunks;

    Buffer sv64Buffer{};
    Buffer chunkBuffer{};

    std::vector<MaterialGpu> materials;
    Buffer materialBuffer{};

    ChunkGridUBO gridInfo{};
    Buffer gridInfoBuffer{};

    ChunkIndexMap chunkIndexMap{};

    std::vector<uint32_t> chunkIndexData;

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

void packChunksForCompute(const class ChunkManager& mgr, WorldComputeGpu& gpuWorld);

}

#endif // RENDERER_COMPUTE_RT_HPP