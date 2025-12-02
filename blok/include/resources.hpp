#ifndef RESOURCES_HPP
#define RESOURCES_HPP
#include "vulkan_context.hpp"
#include <vk_mem_alloc.h>

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

// TODO check if i can use alignas instead of padding
struct alignas(16) FrameUBO {
    // Cam
    glm::mat4 view{};
    glm::mat4 proj{};
    glm::vec3 camPos{};

    float delta_time = 0.0f;

    // Pathtracing
    uint32_t frame_count = 0; // increment each frame
    uint32_t sample_count = 1; // samples per pixel per frame
    float padding1 = 0.0f;
    float padding2 = 0.0f;
};

}

namespace blok {

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

}

namespace blok {

struct AccelerationStructure {
    vk::AccelerationStructureKHR handle{};
    Buffer buffer{};
};

struct WorldSvoGpu {
    std::vector<SvoNode> globalNodes;
    std::vector<ChunkGpu> globalChunks;

    Buffer svoBuffer{};
    Buffer chunkBuffer{};

    AccelerationStructure blas{};
    AccelerationStructure tlas{};

    Buffer blasAabbBuffer{};
    Buffer tlasInstanceBuffer{};
};

}

#endif //RESOURCES_HPP