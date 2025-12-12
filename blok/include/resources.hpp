/*
* File: resources.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef RESOURCES_HPP
#define RESOURCES_HPP
#include "vulkan_context.hpp"
#include <vk_mem_alloc.h>

#include "material.hpp"
#include "svo.hpp"

namespace blok {

struct Buffer {
    vk::Buffer     handle{};
    VmaAllocation  alloc{};
    void*          mapped = nullptr;
    vk::DeviceSize size = 0;
};

enum class ImageKind { Color, Depth, Storage };
struct Image {
    vk::Image               handle{};
    VmaAllocation           alloc{};
    vk::ImageView           view{};
    vk::Format              format{vk::Format::eUndefined};
    uint32_t                width = 0;
    uint32_t                height = 0;
    uint32_t                mipLevels = 1;
    uint32_t                layers = 1;
    vk::SampleCountFlagBits samples{vk::SampleCountFlagBits::e1};
    vk::ImageLayout         currentLayout{vk::ImageLayout::eUndefined};
};

struct Sampler {
    vk::Sampler handle{};
};

struct GBuffer {
    // Current frame output
    Image color; // RGBA32F
    Image worldPosition; // RGBA32F
    Image normalRoughness; // RGBA16F
    Image albedoMetallic; // RGBA8
    Image motionVectors; // RG16F

    // Geometry Validation
    Image worldPositionHistory[2];
    Image normalRoughnessHistory[2];

    // History buffers
    Image historyColor[2];
    Image historyMoments[2]; // RG32F
    Image historyLength[2]; // R16F

    Image variance; // R32F

    Image filterPing; // RGBA32F
    Image filterPong; // RGBA32F

    uint32_t historyIndex = 0;

    Image& currentHistory() { return historyColor[historyIndex]; }
    Image& previousHistory() { return historyColor[1 - historyIndex]; }
    Image& currentMoments() { return historyMoments[historyIndex]; }
    Image& previousMoments() { return historyMoments[1 - historyIndex]; }
    Image& currentHistoryLength() { return historyLength[historyIndex]; }
    Image& previousHistoryLength() { return historyLength[1 - historyIndex]; }
    Image& currentWorldPosition() { return worldPositionHistory[historyIndex]; }
    Image& previousWorldPosition() { return worldPositionHistory[1 - historyIndex]; }
    Image& currentNormalRoughness() { return normalRoughnessHistory[historyIndex]; }
    Image& previousNormalRoughness() { return normalRoughnessHistory[1 - historyIndex]; }

    void swapHistory() { historyIndex = 1 - historyIndex; }
};

struct AtrousPC {
    int stepSize;
    float phiColor;
    float phiNormal;
    float phiDepth;
};

struct FrameResources {
    // Sync
    vk::Semaphore imageAvailable{};
    vk::Semaphore renderFinished{};
    vk::Fence     inFlight{};

    // Commands
    vk::CommandPool   cmdPool{};
    vk::CommandBuffer cmd{};

    // Uniforms
    Buffer frameUBO{};
    vk::DeviceSize uboHead = 0;
};

// TODO I can offload a chunk of this to a PC for the raygen shader
struct alignas(16) FrameUBO {
    // Cam
    glm::mat4 view{};
    glm::mat4 proj{};

    // this is an optimization that will allow the gpu to avoid calculating this for every pixel
    glm::mat4 invView{};
    glm::mat4 invProj{};

    // temporal reprojection
    glm::mat4 prevView{};
    glm::mat4 prevProj{};
    glm::mat4 prevViewProj{};

    glm::vec3 camPos{};
    float delta_time = 0.0f;

    glm::vec3 prevCamPos{};
    int depth = 1; // TODO not used anymore, replaced with sample_count

    // Pathtracing
    uint32_t frame_count = 0; // increment each frame
    uint32_t sample_count = 1; // samples per pixel per frame
    uint32_t screen_width = 0;
    uint32_t screen_height = 0;

    // temporal settings
    float temporalAlpha = 0.05f; // base blend factor
    float momentAlpha = 0.2f; // blend factor for moments
    float varianceClipGamma = 1.0f; // for variance clipping
    float depthThreshold = 0.02f; // depth rejection threshold

    // spatial parameters
    float normalThreshold = 0.9f; // normal rejection threshold
    float phiColor = 4.0f; // color edge-stopping sensitivity
    float phiNormal = 128.0f; // normal edge-stopping sensitivity
    float phiDepth = 1.0f; // depth edge-stopping sensitivity

    // atrous filter params
    int atrousIteration = 0; // the current iteration, to be clear
    int stepSize = 1; // current step size
    float varianceBoost = 1.0f;
    int minHistoryLength = 4;

    // TAA variables (used in raygen)
    glm::vec2 jitterOffset;
    glm::vec2 _padding;
};

}

namespace blok {

// TODO note, deprecated essentially. but kept, as i might find a way to reuse it
struct alignas(16) ChunkGpu {
    uint32_t nodeOffset; // index into global SVO node array
    uint32_t nodeCount;
    uint32_t reserved0;
    uint32_t reserved1;
    glm::vec3 worldMin;
    float pad0;
    glm::vec3 worldMax;
    float pad1;
};
static_assert(sizeof(ChunkGpu) == 48, "expected 48 bytes");

// Each sub-chunk represents a portion of a chunk's SVO
struct alignas(16) SubChunkGpu {
    // SVO navigation
    uint32_t nodeOffset; // Offset into global node array (start of parent chunk's nodes)
    uint32_t rootNodeIndex; // Index of sub-chunk's root node RELATIVE to nodeOffset
    uint32_t nodeCount; // Total nodes in parent chunk (for bounds checking)
    uint32_t startDepth; // Depth at which this sub-chunk starts (for LOD)

    // World-space bounds of this sub-chunk
    glm::vec3 worldMin;
    float subChunkSize; // Size of this sub-chunk in world units

    glm::vec3 worldMax;
    float pad0;
};
static_assert(sizeof(SubChunkGpu) == 48, "expected 48 bytes");

}

namespace blok {

struct AccelerationStructure {
    vk::AccelerationStructureKHR handle{};
    Buffer buffer{};
};

struct WorldSvoGpu {
    std::vector<SvoNode> globalNodes;
    std::vector<SubChunkGpu> globalSubChunks;

    Buffer svoBuffer{};
    Buffer subChunkBuffer{};

    std::vector<MaterialGpu> materials;
    Buffer materialBuffer{};

    AccelerationStructure blas{};
    AccelerationStructure tlas{};

    Buffer blasAabbBuffer{};
    Buffer tlasInstanceBuffer{};
};

}

#endif //RESOURCES_HPP