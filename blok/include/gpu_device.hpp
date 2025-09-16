/*
* File: gpu_device
* Project: blok
* Author: Collin Longoria
* Created on: 9/15/2025
*
* Description: GPU abstraction interface - this is the one overloaded by API
*/
#ifndef BLOK_GPU_DEVICE_HPP
#define BLOK_GPU_DEVICE_HPP

#include <span>
#include <vector>
#include <string>
#include <functional>
#include <optional>

#include "gpu_types.hpp"
#include "gpu_handles.hpp"

/*
 * Copy/Clear Regions
 */

struct BufferCopyRegion {
    size_t srcOffset = 0;
    size_t desOffset = 0;
    size_t size = 0;
};

struct ImageSubresourceRange {
    uint32_t baseMipLevel = 0, mipCount = 1;
    uint32_t baseArrayLayer = 0, layerCount = 1;
};

struct ImageCopyRegion {
    uint32_t srcX = 0, srcY = 0, srcZ = 0;
    uint32_t dstX = 0, dstY = 0, dstZ = 0;
    uint32_t width = 0, height = 0, depth = 1;
    ImageSubresourceRange subresources{};
};

/*
 * Barriers
 */

struct BufferBarrier {
    BufferHandle buffer = 0;
    PipelineStage srcStage = PipelineStage::TOPOFPIPELINE;
    Access srcAccess = Access::NONE;
    PipelineStage dstStage = PipelineStage::COMPUTESHADER;
    Access dstAccess = Access::MEMORYWRITE;
};

struct ImageBarrier {
    ImageHandle image = 0;
    PipelineStage srcStage = PipelineStage::TOPOFPIPELINE;
    Access srcAccess = Access::NONE;
    PipelineStage dstStage = PipelineStage::COMPUTESHADER;
    Access dstAccess = Access::MEMORYWRITE;
    // Note: Layout is backend-specific
};

struct ResourceBarrier {
    enum class Type { BUFFER, IMAGE } type = Type::BUFFER;
    BufferBarrier buf{};
    ImageBarrier img{};
};

/*
 * Render Targets
 */

struct AttachmentDescriptor {
    ImageViewHandle view = 0;
    enum class LoadOperation { LOAD, CLEAR, DONTCARE } load = LoadOperation::CLEAR;
    enum class StoreOperation { STORE, DONTCARE } store = StoreOperation::STORE;
    std::array<float, 4> clearColor{0, 0, 0, 1};
    float clearDepth = 1.0f;
    int clearStencil = 0;
};

struct RenderPassBeginInfo {
    std::vector<AttachmentDescriptor> colorAttachments{};
    std::optional<AttachmentDescriptor> depthAttachments;
    uint32_t width = 0, height = 0;
};

// Only Vulkan/DX12 have dynamic rendering, so we need swapchain
struct SwapchainDescriptor {
    uint32_t width = 0, height = 0;
    Format format = Format::RGBA8_UNORM;
    PresentMode presentMode = PresentMode::VSYNC;
};

/*
 * Command List
 */

class ICommandList {
public:
    virtual ~ICommandList() = default;

    // Initialization and Shudown
    virtual bool initialize(const DeviceInitInfo& info) = 0;
    virtual void shutdown() = 0;

    // Begin and Endpoint
    virtual void begin() = 0;
    virtual void end() = 0;

    // Barriers
    virtual void resourceBarrierBatch(std::span<const ResourceBarrier> barriers) = 0;

    // Copy / Clears
    virtual void copyBuffer(BufferHandle src, BufferHandle dst, std::span<const BufferCopyRegion> regions) = 0;
    virtual void copyBufferToImage(BufferHandle src, ImageHandle dst, const ImageCopyRegion& region) = 0;
    virtual void copyImageToBuffer(ImageHandle src, BufferHandle dst, const ImageCopyRegion& region) = 0;
    virtual void copyImage(ImageHandle src, ImageHandle dst, const ImageCopyRegion& region) = 0;
    virtual void clearBuffer(BufferHandle dst, size_t offset, size_t size, uint32_t value) = 0;
    virtual void clearImage(ImageHandle dst, const ImageSubresourceRange& sub, const std::array<float, 4>& rgba) = 0;

    // Binding
    virtual void bindPipeline(GraphicsPipelineHandle pipeline) = 0;
    virtual void bindPipeline(ComputePipelineHandle pipeline) = 0;
    virtual void bindBindGroup(uint32_t setIndex, BindGroupHandle group) = 0;
    virtual void pushConstants(uint32_t offsetBytes, uint32_t sizeBytes, const void* data) = 0;

    // Compute
    virtual void dispatch(uint32_t gx, uint32_t gy, uint32_t gz) = 0;
    virtual void dispatchIndirect(BufferHandle args, size_t offset) = 0;

    // Graphics
    virtual void beginRenderPass(const RenderPassBeginInfo& info) = 0;
    virtual void endRenderPass() = 0;
    virtual void setViewport(float x, float y, float w, float h, float minDepth=0.0f, float maxDepth=1.0f) = 0;
    virtual void setScissor(float x, float y, float width, float height) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, IndexType type, size_t offset) = 0;
    virtual void bindVertexBuffers(uint32_t firstBinding, std::span<const BufferHandle> buffers, std::span<const size_t> offsets) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
};

/*
 * Submission / Sync
 */

struct WaitSemaphore {
    SemaphoreHandle semaphore = 0;
    uint64_t value = 0;
    PipelineStage stage = PipelineStage::BOTTOMOFPIPELINE;
};

struct SubmitBatch {
    QueueType queue = QueueType::COMPUTE;
    std::span<ICommandList*> lists;
    std::span<const WaitSemaphore> waits{};
    // FenceHandle fence = 0;
};

/*
 * Device
 */

class IGPUDevice {
public:
    virtual ~IGPUDevice() = default;

    // Capabilities
    virtual DeviceCapabilities getCapabilities() const = 0;

    // Resouce creation / destruction
    virtual BufferHandle createBuffer(const BufferDescriptor&, const void* initialData=nullptr) = 0;
    virtual void destroyBuffer(BufferHandle) = 0;

    virtual ImageHandle createImage(const ImageDescriptor&, const void* initialPixels=nullptr) = 0;
    virtual void destroyImage(ImageHandle) = 0;

    virtual ImageViewHandle createImageView(ImageHandle, const ImageViewDescriptor&) = 0;
    virtual void destroyImageView(ImageViewHandle) = 0;

    virtual SamplerHandle createSampler(const SamplerDescriptor&) = 0;
    virtual void destroySampler(SamplerHandle) = 0;

    virtual BindGroupLayoutHandle createBindGroupLayout(const BindGroupLayoutDescriptor&) = 0;
    virtual void destroyBindGroupLayout(BindGroupLayoutHandle) = 0;

    struct BindGroupEntry {
        uint32_t binding;
        enum class Kind{ BUFFER, IMAGEVIEW, SAMPLER } kind;
        uint64_t handle;
        size_t offset = 0;
        size_t size = 0;
    };
    struct BindGroupDescriptor {
        BindGroupLayoutHandle layout;
        std::vector<BindGroupEntry> entries;
    };

    virtual BindGroupHandle createBindGroup(const BindGroupDescriptor&) = 0;
    virtual void destroyBindGroup(BindGroupHandle) = 0;

    virtual PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDescriptor&) = 0;
    virtual void destroyPipelineLayout(PipelineLayoutHandle) = 0;

    virtual ShaderModuleHandle createShaderModule(const ShaderModuleDescriptor&) = 0;
    virtual void destroyShaderModule(ShaderModuleHandle) = 0;

    virtual GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDescriptor&) = 0;
    virtual void destroyGraphicsPipeline(GraphicsPipelineHandle) = 0;

    virtual ComputePipelineHandle createComputePipeline(const ComputePipelineDescriptor&) = 0;
    virtual void destroyComputePipeline(ComputePipelineHandle) = 0;

    virtual SwapchainHandle createSwapchain(const SwapchainDescriptor&) = 0;
    virtual void destroySwapchain(SwapchainHandle) = 0;

    // Per-frame Render
    virtual ImageViewHandle acquireNextImage(SwapchainHandle) = 0;
    virtual void present(SwapchainHandle) = 0;

    // Mapping / Updates
    virtual void* mapBuffer(BufferHandle, size_t offset, size_t size) = 0;
    virtual void unmapBuffer(BufferHandle) = 0;
    virtual void updateBuffer(BufferHandle, size_t offset, size_t size, const void* data) = 0;

    // Command Lists & Submission
    virtual ICommandList* createCommandList(QueueType) = 0;
    virtual void destroyCommandList(ICommandList*) = 0;

    virtual void submit(const SubmitBatch&) = 0;
    virtual void waitIdle(QueueType) = 0;

    // Sync Primitives
    virtual FenceHandle createFence(bool signaled=false) = 0;
    virtual void destroyFence(FenceHandle) = 0;
    virtual void waitForFences(std::span<const FenceHandle> fences, bool waitAll=true, uint64_t timeoutNs=~0ull) = 0;

    enum class SemaphoreType { BINARY, TIMELINE };
    virtual SemaphoreHandle createSemaphore(SemaphoreType type, uint64_t initialValue=0) = 0;
    virtual void destroySemaphore(SemaphoreHandle) = 0;

    // Queries
    virtual QueryPoolHandle createTimestampQueryPool(uint32_t count) = 0;
    virtual void destroyQueryPool(QueryPoolHandle) = 0;
    virtual bool getQueryResults(QueryPoolHandle, uint32_t first, uint32_t count, std::span<uint64_t> outTimestampNs) = 0;

};

#endif //BLOK_GPU_DEVICE_HPP