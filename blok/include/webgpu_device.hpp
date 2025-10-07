/*
* File: webgpu_device
* Project: blok
* Author: Collin Longoria
* Created on: 9/16/2025
*
* Description: Implementation of the GPU Device and Command List interfaces utilizing the WebGPU API (Dawn)
*/
#ifndef BLOK_WEBGPU_DEVICE_HPP
#define BLOK_WEBGPU_DEVICE_HPP

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <thread>
#include <webgpu.h>

#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include "gpu_handles.hpp"
#include "gpu_types.hpp"
#include "gpu_types.hpp"

namespace blok {
// For printing WGPUStringView (which may not be null terminated, as I learned)
static std::string to_string_view(WGPUStringView v) {
    std::string s;
    if (v.data && v.length) s.assign(v.data, v.length);
    return s;
}

/*
 * Resource Handle Pools
 * Reference: https://sourcemaking.com/design_patterns/object_pool/cpp/1
 */
// TODO: Ensure thread safety
template<typename T>
struct Pool {
private:
    std::unordered_map<uint64_t, T> objects;
    uint64_t next{1};
public:
    uint64_t add(T obj) noexcept {
        const uint64_t h = next++;
        objects.emplace(h, std::move(obj));
        return h;
    }
    T* get(uint64_t h) {
        auto it = objects.find(h);
        return (it == objects.end()) ? nullptr : &it->second;
    }
    [[nodiscard]] const T* get(uint64_t h) const {
        auto it = objects.find(h);
        return (it == objects.end()) ? nullptr : &it->second;
    }
    void remove(uint64_t h) {
        objects.erase(h);
    }
    void clear() {
        objects.clear();
        next = 1;
    }
};

/*
 * Format mapping
 */
namespace detail {
    inline WGPUIndexFormat toWGPU(IndexType t) {
        switch (t) {
        case IndexType::UINT16: return WGPUIndexFormat_Uint16;
        case IndexType::UINT32: return WGPUIndexFormat_Uint32;
        }
        return WGPUIndexFormat_Undefined;
    }

    inline WGPUTextureFormat toWGPU(Format f) {
        switch (f) {
        case Format::UNKNOWN: return WGPUTextureFormat_Undefined;
        case Format::R8_UNORM: return WGPUTextureFormat_R8Unorm;
        case Format::RG8_UNORM: return WGPUTextureFormat_RG8Unorm;
        case Format::RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
        case Format::BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
        case Format::BGRA8_UNORM_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
        case Format::RGBA8_UNORM_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
        case Format::R8_UINT: return WGPUTextureFormat_R8Uint;
        case Format::R16_UINT: return WGPUTextureFormat_R16Uint;
        case Format::R32_UINT: return WGPUTextureFormat_R32Uint;
        case Format::RGBA32_UINT: return WGPUTextureFormat_RGBA32Uint;
        case Format::R16_FLOAT: return WGPUTextureFormat_R16Float;
        case Format::R32_FLOAT: return WGPUTextureFormat_R32Float;
        case Format::RG32_FLOAT: return WGPUTextureFormat_RG32Float;
        case Format::RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
        case Format::RGBA32_FLOAT: return WGPUTextureFormat_RGBA32Float;
        case Format::D24S8: return WGPUTextureFormat_Depth24PlusStencil8;
        case Format::D32_FLOAT: return WGPUTextureFormat_Depth32Float;
        case Format::PREFERRED: return WGPUTextureFormat_Undefined;
        }
        return WGPUTextureFormat_Undefined;
    }

    inline Format fromWGPU(WGPUTextureFormat f) {
        switch (f) {
        case WGPUTextureFormat_Undefined: return Format::UNKNOWN;
        case WGPUTextureFormat_R8Unorm: return Format::R8_UNORM;
        case WGPUTextureFormat_RG8Unorm: return Format::RG8_UNORM;
        case WGPUTextureFormat_RGBA8Unorm: return Format::RGBA8_UNORM;
        case WGPUTextureFormat_BGRA8Unorm: return Format::BGRA8_UNORM;
        case WGPUTextureFormat_BGRA8UnormSrgb: return Format::BGRA8_UNORM_SRGB;
        case WGPUTextureFormat_RGBA8UnormSrgb: return Format::RGBA8_UNORM_SRGB;
        case WGPUTextureFormat_R8Uint: return Format::R8_UINT;
        case WGPUTextureFormat_R16Uint: return Format::R16_UINT;
        case WGPUTextureFormat_R32Uint: return Format::R32_UINT;
        case WGPUTextureFormat_RGBA32Uint: return Format::RGBA32_UINT;
        case WGPUTextureFormat_R16Float: return Format::R16_FLOAT;
        case WGPUTextureFormat_R32Float: return Format::R32_FLOAT;
        case WGPUTextureFormat_RG32Float: return Format::RG32_FLOAT;
        case WGPUTextureFormat_RGBA16Float: return Format::RGBA16_FLOAT;
        case WGPUTextureFormat_RGBA32Float: return Format::RGBA32_FLOAT;
        case WGPUTextureFormat_Depth24PlusStencil8: return Format::D24S8;
        case WGPUTextureFormat_Depth32Float: return Format::D32_FLOAT;
        default: return Format::UNKNOWN;
        }
    }

    inline Format toSRGB(Format f) {
        switch (f) {
        case Format::RGBA8_UNORM: return Format::RGBA8_UNORM_SRGB;
        case Format::BGRA8_UNORM: return Format::BGRA8_UNORM_SRGB;
        default: return f;
        }
    }

    inline WGPUTextureDimension toWGPU(ImageDimensions d) {
        switch (d) {
        case ImageDimensions::D1: return WGPUTextureDimension_1D;
        case ImageDimensions::D2: return WGPUTextureDimension_2D;
        case ImageDimensions::D3: return WGPUTextureDimension_3D;
        }
        return WGPUTextureDimension_Undefined;
    }

    inline WGPUFilterMode toWGPU(SamplerDescriptor::Filter f) {
        return f == SamplerDescriptor::Filter::NEAREST ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    }

    inline WGPUMipmapFilterMode toWGPU(SamplerDescriptor::MipFilter f) {
        return f == SamplerDescriptor::MipFilter::NEAREST ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
    }

    inline WGPUAddressMode toWGPU(SamplerDescriptor::Address a) {
        switch (a) {
        case SamplerDescriptor::Address::REPEAT: return WGPUAddressMode_Repeat;
        case SamplerDescriptor::Address::CLAMP: return WGPUAddressMode_ClampToEdge;
        case SamplerDescriptor::Address::MIRROR: return WGPUAddressMode_MirrorRepeat;
        }
        return WGPUAddressMode_Repeat;
    }

    inline WGPUPresentMode toWGPU(PresentMode m) {
        switch (m) {
        case PresentMode::IMMEDIATE: return WGPUPresentMode_Immediate;
        case PresentMode::VSYNC: return WGPUPresentMode_Fifo;
        case PresentMode::MAILBOX: return WGPUPresentMode_Mailbox;
        }
        return WGPUPresentMode_Fifo;
    }

    inline WGPUPrimitiveTopology toWGPU(PrimitiveTopology t) {
        switch (t) {
        case PrimitiveTopology::TRIANGLELIST: return WGPUPrimitiveTopology_TriangleList;
        case PrimitiveTopology::TRIANGLESTRIP: return WGPUPrimitiveTopology_TriangleStrip;
        case PrimitiveTopology::LINELIST: return WGPUPrimitiveTopology_LineList;
        case PrimitiveTopology::POINTLIST: return WGPUPrimitiveTopology_PointList;
        }
        return WGPUPrimitiveTopology_TriangleList;
    }

    inline WGPUFrontFace toWGPU(FrontFace f) {
        return f == FrontFace::CCW ? WGPUFrontFace_CCW : WGPUFrontFace_CW;
    }

    inline WGPUCullMode toWGPU(CullMode c) {
        switch (c) {
        case CullMode::NONE: return WGPUCullMode_None;
        case CullMode::BACK: return WGPUCullMode_Back;
        case CullMode::FRONT: return WGPUCullMode_Front;
        }
        return WGPUCullMode_Back; // backface culling by default? discuss.
    }

    inline WGPUShaderStage toWGPU(PipelineStage stages) {
        WGPUShaderStage flags = 0;
        if ((stages & PipelineStage::VERTEXSHADER) != PipelineStage{}) flags |= WGPUShaderStage_Vertex;
        if ((stages & PipelineStage::FRAGMENTSHADER) != PipelineStage{}) flags |= WGPUShaderStage_Fragment;
        if ((stages & PipelineStage::COMPUTESHADER) != PipelineStage{}) flags |= WGPUShaderStage_Compute;
        return flags;
    }

    inline WGPUBufferUsage toWGPU(BufferUsage u) {
        WGPUBufferUsage flags = 0;
        if ((u & BufferUsage::VERTEX) != BufferUsage{}) flags |= WGPUBufferUsage_Vertex;
        if ((u & BufferUsage::INDEX) != BufferUsage{}) flags |= WGPUBufferUsage_Index;
        if ((u & BufferUsage::UNIFORM) != BufferUsage{}) flags |= WGPUBufferUsage_Uniform;
        if ((u & BufferUsage::STORAGE) != BufferUsage{}) flags |= WGPUBufferUsage_Storage;
        if ((u & BufferUsage::INDIRECT) != BufferUsage{}) flags |= WGPUBufferUsage_Indirect;
        if ((u & BufferUsage::COPYSOURCE) != BufferUsage{}) flags |= WGPUBufferUsage_CopySrc;
        if ((u & BufferUsage::COPYDESTINATION) != BufferUsage{}) flags |= WGPUBufferUsage_CopyDst;
        return flags;
    }

    inline WGPUTextureUsage toWGPU(ImageUsage u) {
        WGPUTextureUsage flags = 0;
        if ((u & ImageUsage::SAMPLED) != ImageUsage{}) flags |= WGPUTextureUsage_TextureBinding;
        if ((u & ImageUsage::STORAGE) != ImageUsage{}) flags |= WGPUTextureUsage_StorageBinding;
        if ((u & ImageUsage::COLOR) != ImageUsage{}) flags |= WGPUTextureUsage_RenderAttachment;
        if ((u & ImageUsage::DEPTH) != ImageUsage{}) flags |= WGPUTextureUsage_RenderAttachment;
        if ((u & ImageUsage::COPYSOURCE) != ImageUsage{}) flags |= WGPUTextureUsage_CopySrc;
        if ((u & ImageUsage::COPYDESTINATION) != ImageUsage{}) flags |= WGPUTextureUsage_CopyDst;
        return flags;
    }

    inline WGPUBackendType toWGPU(RenderBackend b) {
        switch (b) {
        case RenderBackend::OpenGL: /* */ break;
        case RenderBackend::CUDA: /* */ break;
        case RenderBackend::WEBGPU_D3D12: return WGPUBackendType_D3D12;
        case RenderBackend::WEBGPU_VULKAN: return WGPUBackendType_Vulkan;
        }
#if(_WIN32)
        return WGPUBackendType_D3D12;
#else
        return WGPUBackendType_Vulkan;
#endif
    }

    inline uint32_t bytesPerPixel(Format f) {
        switch (f) {
        case Format::R8_UNORM: return 1;
        case Format::RG8_UNORM: return 2;
        case Format::RGBA8_UNORM: return 4;
        case Format::R8_UINT: return 1;
        case Format::R16_UINT: return 2;
        case Format::R32_UINT: return 4;
        case Format::RGBA32_UINT: return 16;
        case Format::R16_FLOAT: return 2;
        case Format::R32_FLOAT: return 4;
        case Format::RGBA16_FLOAT: return 8;
        case Format::RGBA32_FLOAT: return 16;
        case Format::D24S8: return 4;
        case Format::D32_FLOAT: return 4;
        default: return 4;
        }
    }

    inline WGPUVertexFormat toWGPUVertexFormat(Format f) {
        switch (f) {
        case Format::R32_FLOAT: return WGPUVertexFormat_Float32;
        case Format::RGBA32_FLOAT: return WGPUVertexFormat_Float32x4;
        case Format::R16_FLOAT: return WGPUVertexFormat_Float16;
        case Format::RGBA16_FLOAT: return WGPUVertexFormat_Float16x4;
        case Format::R32_UINT: return WGPUVertexFormat_Uint32;
        case Format::RGBA32_UINT: return WGPUVertexFormat_Uint32x4;
        case Format::R8_UNORM: return WGPUVertexFormat_Unorm8;
        case Format::RG8_UNORM: return WGPUVertexFormat_Unorm8x2;
        case Format::RGBA8_UNORM: return WGPUVertexFormat_Unorm8x4;
        default: return WGPUVertexFormat_Float32; // TODO: no undefined - might throw error here
        }
    }

    inline bool isDepthFormat(Format f) {
        return f == Format::D24S8 || f == Format::D32_FLOAT;
    }
}

/*
 * Internal resource records
 */
struct BufferRecord {
    WGPUBuffer buffer{};
    size_t size{};
};

struct ImageRecord {
    WGPUTexture texture{};
    ImageDescriptor descriptor{};
};

struct ImageViewRecord {
    WGPUTextureView textureView{};
};

struct SamplerRecord {
    WGPUSampler sampler{};
};

struct BindGroupLayoutRecord {
    WGPUBindGroupLayout layout{};
    std::vector<BindGroupLayoutEntry> entries;
};

struct BindGroupRecord {
    WGPUBindGroup bindGroup{};
};

struct PipelineLayoutRecord {
    WGPUPipelineLayout layout{};
};

struct ShaderRecord {
    WGPUShaderModule module{};
    ShaderModuleDescriptor descriptor;
};

struct GraphicsPipelineRecord {
    WGPURenderPipeline pipeline{};
};

struct ComputePipelineRecord {
    WGPUComputePipeline pipeline{};
};

struct SwapchainRecord {
    WGPUSurface surface{};
    WGPUSurfaceConfiguration configuration{};
    WGPUTexture currentTexture{};
    WGPUTextureView currentView{};
};

struct QuerySetRecord {
    WGPUQuerySet querySet{};
    uint32_t count{};
};

// Forward decls
class WebGPUDevice;
class WebGPUCommandList;

/*
 * Internal resource records
 */
class WebGPUCommandList final : public ICommandList {
public:
    explicit WebGPUCommandList(WebGPUDevice* owner, QueueType q) : m_device(owner), m_queueType(q) {}
    ~WebGPUCommandList() override;

    void begin() override;
    void end() override;

    void resourceBarrierBatch(std::span<const ResourceBarrier> barriers) override { /* WebGPU handles this */}

    void copyBuffer(BufferHandle src, BufferHandle dst, std::span<const BufferCopyRegion> regions) override;
    void copyBufferToImage(BufferHandle src, ImageHandle dst, const ImageCopyRegion &region) override;
    void copyImageToBuffer(ImageHandle src, BufferHandle dst, const ImageCopyRegion &region) override;
    void copyImage(ImageHandle src, ImageHandle dst, const ImageCopyRegion &region) override;
    void clearBuffer(BufferHandle dst, size_t offset, size_t size, uint32_t value) override;
    void clearImage(ImageHandle dst, const ImageSubresourceRange &sub, const std::array<float, 4> &rgba) override;

    void bindGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
    void bindComputePipeline(ComputePipelineHandle pipeline) override;
    void bindBindGroup(uint32_t setIndex, BindGroupHandle group) override;
    void pushConstants(uint32_t offsetBytes, uint32_t sizeBytes, const void *data) override {
        /* WebGPU does not expose push constants */
    }

    void dispatch(uint32_t gx, uint32_t gy, uint32_t gz) override;
    void dispatchIndirect(BufferHandle args, size_t offset) override;

    void beginRenderPass(const RenderPassBeginInfo &info) override;
    void endRenderPass() override;
    void setViewport(float x, float y, float w, float h, float minDepth, float maxDepth) override;
    void setScissor(float x, float y, float width, float height) override;
    void bindIndexBuffer(BufferHandle buffer, IndexType type, size_t offset) override;
    void bindVertexBuffers(uint32_t firstBinding, std::span<const BufferHandle> buffers, std::span<const size_t> offsets) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;

    [[nodiscard]] WGPUCommandBuffer commandBuffer() const { return m_commandBuffer; }

private:
    WebGPUDevice* m_device{};
    QueueType m_queueType{};

    WGPUCommandEncoder m_encoder{};
    WGPURenderPassEncoder m_renderPass{};
    WGPUComputePassEncoder m_computePass{};
    WGPUCommandBuffer m_commandBuffer{};

    GraphicsPipelineHandle m_currentGraphicsPipeline{};
    ComputePipelineHandle m_currentComputePipeline{};

    void ensureComputePass();
    [[nodiscard]] WGPUDevice deviceWGPU() const;
};

/*
 * Internal resource records
 */
class WebGPUDevice final : public IGPUDevice {
public:
    explicit WebGPUDevice(const DeviceInitInfo& init);
    ~WebGPUDevice() override;

    [[nodiscard]] DeviceCapabilities getCapabilities() const override { return m_deviceCapabilities; }

    BufferHandle createBuffer(const BufferDescriptor &, const void *initialData) override;
    void destroyBuffer(BufferHandle) override;

    ImageHandle createImage(const ImageDescriptor &, const void *initialPixels) override;
    void destroyImage(ImageHandle) override;

    ImageViewHandle createImageView(ImageHandle, const ImageViewDescriptor &) override;
    void destroyImageView(ImageViewHandle) override;

    SamplerHandle createSampler(const SamplerDescriptor &) override;
    void destroySampler(SamplerHandle) override;

    BindGroupLayoutHandle createBindGroupLayout(const BindGroupLayoutDescriptor &) override;
    void destroyBindGroupLayout(BindGroupLayoutHandle) override;

    BindGroupHandle createBindGroup(const BindGroupDescriptor &) override;
    void destroyBindGroup(BindGroupHandle) override;

    PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDescriptor &) override;
    void destroyPipelineLayout(PipelineLayoutHandle) override;

    ShaderModuleHandle createShaderModule(const ShaderModuleDescriptor &) override;
    void destroyShaderModule(ShaderModuleHandle) override;

    GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDescriptor &) override;
    void destroyGraphicsPipeline(GraphicsPipelineHandle) override;

    ComputePipelineHandle createComputePipeline(const ComputePipelineDescriptor &) override;
    void destroyComputePipeline(ComputePipelineHandle) override;

    SwapchainHandle createSwapchain(const SwapchainDescriptor &) override;
    void destroySwapchain(SwapchainHandle) override;
    ImageViewHandle acquireNextImage(SwapchainHandle) override;
    void present(SwapchainHandle) override;

    void *mapBuffer(BufferHandle, size_t offset, size_t size) override;
    void unmapBuffer(BufferHandle) override;
    void updateBuffer(BufferHandle, size_t offset, size_t size, const void *data) override;

    ICommandList *createCommandList(QueueType) override;
    void destroyCommandList(ICommandList *) override;
    void submit(const SubmitBatch &) override;
    void waitIdle(QueueType) override;

    // These are not supported by WebGPU
    FenceHandle createFence(bool signaled) override { return 0; }
    void destroyFence(FenceHandle) override { }
    void waitForFences(std::span<const FenceHandle> fences, bool waitAll, uint64_t timeoutNs) override { }

    SemaphoreHandle createSemaphore(SemaphoreType type, uint64_t initialValue) override { return 0; }
    void destroySemaphore(SemaphoreHandle) override { }

    QueryPoolHandle createTimestampQueryPool(uint32_t count) override;
    void destroyQueryPool(QueryPoolHandle) override;
    bool getQueryResults(QueryPoolHandle, uint32_t first, uint32_t count, std::span<uint64_t> outTimestampNs) override;

    [[nodiscard]] WGPUDevice deviceWGPU() const { return (m_device); }
    [[nodiscard]] Format backbufferFormat() const { return m_backbufferFormat; }

private:
    friend class WebGPUCommandList;

    WGPUInstance m_instance{};
    WGPUAdapter m_adapter{};
    WGPUDevice m_device{};
    WGPUQueue m_queue{};
    WGPUSurface m_surface{}; bool m_surfaceConfigured = false;

    Format m_backbufferFormat = Format::RGBA8_UNORM;
    PresentMode m_presentMode = PresentMode::VSYNC;
    uint32_t m_framebufferWidth = 0, m_framebufferHeight = 0;

    DeviceCapabilities m_deviceCapabilities{};

    blok::Pool<BufferRecord> m_bufferPool{};
    blok::Pool<ImageRecord> m_imagePool{};
    blok::Pool<ImageViewRecord> m_imageViewPool{};
    blok::Pool<SamplerRecord> m_samplerPool{};
    blok::Pool<BindGroupLayoutRecord> m_bindGroupLayoutPool{};
    blok::Pool<BindGroupRecord> m_bindGroupPool{};
    blok::Pool<PipelineLayoutRecord> m_pipelineLayoutPool{};
    blok::Pool<ShaderRecord> m_shaderPool{};
    blok::Pool<GraphicsPipelineRecord> m_graphicsPipelinePool{};
    blok::Pool<ComputePipelineRecord> m_computePipelinePool{};
    blok::Pool<SwapchainRecord> m_swapchainPool{};
    blok::Pool<QuerySetRecord> m_querySetPool{};
};

/*
 * IMPLEMENTATIONS
 * Abandon all hope, ye who enter here!
 */

inline WebGPUCommandList::~WebGPUCommandList() {
    if (m_renderPass) { wgpuRenderPassEncoderEnd(m_renderPass); m_renderPass = nullptr; }
    if (m_computePass) { wgpuComputePassEncoderEnd(m_computePass); m_computePass = nullptr; }
    if (m_encoder) { wgpuCommandEncoderRelease(m_encoder); m_encoder = nullptr; }
    if (m_commandBuffer) { wgpuCommandBufferRelease(m_commandBuffer); m_commandBuffer = nullptr; }
}

inline void WebGPUCommandList::begin() {
    assert(!m_encoder && "[FATAL] WebGPUCommandList: begin() called twice");
    WGPUCommandEncoderDescriptor d = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    d.label = WGPU_STRING_VIEW_INIT;
    m_encoder = wgpuDeviceCreateCommandEncoder(deviceWGPU(), &d);
}

inline void WebGPUCommandList::end() {
    if (m_renderPass) { wgpuRenderPassEncoderEnd(m_renderPass); m_renderPass = nullptr; }
    if (m_computePass) { wgpuComputePassEncoderEnd(m_computePass); m_computePass = nullptr; }
    WGPUCommandBufferDescriptor bd = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    m_commandBuffer = wgpuCommandEncoderFinish(m_encoder, &bd);
    wgpuCommandEncoderRelease(m_encoder);
    m_encoder = nullptr;
}

inline void WebGPUCommandList::copyBuffer(BufferHandle src, BufferHandle dst, std::span<const BufferCopyRegion> regions) {
    auto* s = m_device->m_bufferPool.get(src);
    auto* d = m_device->m_bufferPool.get(dst);
    for (auto& r : regions) {
        wgpuCommandEncoderCopyBufferToBuffer(m_encoder, s->buffer, r.srcOffset, d->buffer, r.desOffset, r.size);
    }
}

inline void WebGPUCommandList::copyBufferToImage(BufferHandle src, ImageHandle dst, const ImageCopyRegion &region) {
    auto* s = m_device->m_bufferPool.get(src);
    auto* d = m_device->m_imagePool.get(dst);

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = detail::bytesPerPixel(d->descriptor.format) * region.width;
    layout.rowsPerImage = region.height;

    WGPUTexelCopyBufferInfo bi = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    bi.buffer = s->buffer;
    bi.layout = layout;

    WGPUTexelCopyTextureInfo ti = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    ti.texture = d->texture;
    ti.mipLevel = region.subresources.baseMipLevel;
    ti.origin = { region.dstX, region.dstY, region.dstZ };
    ti.aspect = detail::isDepthFormat(d->descriptor.format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;

    WGPUExtent3D size{ region.width, region.height, region.depth };
    wgpuCommandEncoderCopyBufferToTexture(m_encoder, &bi, &ti, &size);
}

inline void WebGPUCommandList::copyImageToBuffer(ImageHandle src, BufferHandle dst, const ImageCopyRegion &region) {
    auto* s = m_device->m_imagePool.get(src);
    auto* d = m_device->m_bufferPool.get(dst);

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = detail::bytesPerPixel(s->descriptor.format) * region.width;
    layout.rowsPerImage = region.height;

    WGPUTexelCopyBufferInfo bi = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    bi.buffer = d->buffer;
    bi.layout = layout;

    WGPUTexelCopyTextureInfo ti = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    ti.texture = s->texture;
    ti.mipLevel = region.subresources.baseMipLevel;
    ti.origin = { region.srcX, region.srcY, region.srcZ };
    ti.aspect = detail::isDepthFormat(s->descriptor.format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;

    WGPUExtent3D size{ region.width, region.height, region.depth };
    wgpuCommandEncoderCopyTextureToBuffer(m_encoder, &ti, &bi, &size);
}

inline void WebGPUCommandList::copyImage(ImageHandle src, ImageHandle dst, const ImageCopyRegion &region) {
    auto* s = m_device->m_imagePool.get(src);
    auto* d = m_device->m_imagePool.get(dst);

    WGPUTexelCopyTextureInfo si = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    si.texture = s->texture;
    si.mipLevel = region.subresources.baseMipLevel;
    si.origin = { region.srcX, region.srcY, region.srcZ };
    si.aspect = detail::isDepthFormat(s->descriptor.format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;

    WGPUTexelCopyTextureInfo di = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    di.texture = d->texture;
    di.mipLevel = region.subresources.baseMipLevel;
    di.origin = { region.dstX, region.dstY, region.dstZ };
    di.aspect = detail::isDepthFormat(d->descriptor.format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;

    WGPUExtent3D size{ region.width, region.height, region.depth };
    wgpuCommandEncoderCopyTextureToTexture(m_encoder, &si, &di, &size);
}

inline void WebGPUCommandList::clearBuffer(BufferHandle dst, size_t offset, size_t size, uint32_t value) {
    // WebGPU does not allow clearing to arbitrary values, defaults to 0
    (void)value;

    auto* d = m_device->m_bufferPool.get(dst);
    wgpuCommandEncoderClearBuffer(m_encoder, d->buffer, offset, size);
}

inline void WebGPUCommandList::clearImage(ImageHandle dst, const ImageSubresourceRange &sub, const std::array<float, 4> &rgba) {
    auto* img = m_device->m_imagePool.get(dst);
    const bool isDepth = detail::isDepthFormat(img->descriptor.format);

    // Transient view for the specified subresource range
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    vd.baseMipLevel = sub.baseMipLevel;
    vd.mipLevelCount = sub.mipCount;
    vd.baseArrayLayer = sub.baseArrayLayer;
    vd.arrayLayerCount = sub.layerCount;
    vd.aspect = isDepth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(img->texture, &vd);

    // Tiny render pass, that just clears
    if (!isDepth) {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view;
        color.resolveTarget = nullptr;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Discard;
        color.clearValue = { rgba[0], rgba[1], rgba[2], rgba[3] };

        WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &color;

        rp.depthStencilAttachment = nullptr;

        m_renderPass = wgpuCommandEncoderBeginRenderPass(m_encoder, &rp);
        wgpuRenderPassEncoderEnd(m_renderPass);
        m_renderPass = nullptr;
    }
    else {
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = view;
        ds.depthClearValue = rgba[0];
        ds.depthLoadOp = WGPULoadOp_Clear;
        ds.depthStoreOp = WGPUStoreOp_Discard;

        WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rp.colorAttachmentCount = 0;
        rp.colorAttachments = nullptr;
        rp.depthStencilAttachment = &ds;

        m_renderPass = wgpuCommandEncoderBeginRenderPass(m_encoder, &rp);
        wgpuRenderPassEncoderEnd(m_renderPass);
        m_renderPass = nullptr;
    }

    wgpuTextureViewRelease(view);
}

inline void WebGPUCommandList::bindGraphicsPipeline(GraphicsPipelineHandle pipeline) {
    m_currentGraphicsPipeline = pipeline;
    if (m_renderPass) {
        auto p = m_device->m_graphicsPipelinePool.get(pipeline);
        wgpuRenderPassEncoderSetPipeline(m_renderPass, p->pipeline);
    }
}

inline void WebGPUCommandList::bindComputePipeline(ComputePipelineHandle pipeline) {
    m_currentComputePipeline = pipeline;
    ensureComputePass();
    auto p = m_device->m_computePipelinePool.get(pipeline);
    wgpuComputePassEncoderSetPipeline(m_computePass, p->pipeline);
}

inline void WebGPUCommandList::bindBindGroup(uint32_t setIndex, BindGroupHandle group) {
    if (m_renderPass) {
        auto g = m_device->m_bindGroupPool.get(group);
        wgpuRenderPassEncoderSetBindGroup(m_renderPass, setIndex, g->bindGroup, 0, nullptr);
    }
    else {
        ensureComputePass();
        auto g = m_device->m_bindGroupPool.get(group);
        wgpuComputePassEncoderSetBindGroup(m_computePass, setIndex, g->bindGroup, 0, nullptr);
    }
}

inline void WebGPUCommandList::dispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    ensureComputePass();
    wgpuComputePassEncoderDispatchWorkgroups(m_computePass, gx, gy, gz);
}

inline void WebGPUCommandList::dispatchIndirect(BufferHandle args, size_t offset) {
    ensureComputePass();
    auto b = m_device->m_bufferPool.get(args);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(m_computePass, b->buffer, offset);
}

inline void WebGPUCommandList::beginRenderPass(const RenderPassBeginInfo &info) {
    // Color attachments
    std::vector<WGPURenderPassColorAttachment> cols;
    cols.reserve(info.colorAttachments.size());
    for (auto const& a : info.colorAttachments) {
        auto* v = m_device->m_imageViewPool.get(a.view);
        WGPURenderPassColorAttachment c = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        c.view = v->textureView;
        c.resolveTarget = nullptr;
        switch (a.load) {
        case AttachmentDescriptor::LoadOperation::LOAD:
            c.loadOp = WGPULoadOp_Load;
            break;
        case AttachmentDescriptor::LoadOperation::CLEAR:
            c.loadOp = WGPULoadOp_Clear;
            c.clearValue = { a.clearColor[0], a.clearColor[1], a.clearColor[2], a.clearColor[3] };
            break;
        case AttachmentDescriptor::LoadOperation::DONTCARE:
            c.loadOp = WGPULoadOp_Undefined;
            break;
        }
        c.storeOp = (a.store == AttachmentDescriptor::StoreOperation::STORE) ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
        cols.push_back(c);
    }

    // Depth
    WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassDepthStencilAttachment* pds = nullptr;
    if (info.depthAttachments.has_value()) {
        auto const& a = info.depthAttachments.value();
        auto* v = m_device->m_imageViewPool.get(a.view);
        ds.view = v->textureView;
        ds.depthReadOnly = false;
        switch (a.load) {
        case AttachmentDescriptor::LoadOperation::LOAD:
            ds.depthLoadOp = WGPULoadOp_Load;
            break;
        case AttachmentDescriptor::LoadOperation::CLEAR:
            ds.depthLoadOp = WGPULoadOp_Clear;
            ds.depthClearValue = a.clearColor[0];
            break;
        case AttachmentDescriptor::LoadOperation::DONTCARE:
            ds.depthLoadOp = WGPULoadOp_Undefined;
            break;
        }
        ds.depthStoreOp = (a.store == AttachmentDescriptor::StoreOperation::STORE) ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
        pds = &ds;
    }

    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rp.colorAttachmentCount = cols.size();
    rp.colorAttachments = cols.empty() ? nullptr : cols.data();
    rp.depthStencilAttachment = pds;

    m_renderPass = wgpuCommandEncoderBeginRenderPass(m_encoder, &rp);
    if (m_currentGraphicsPipeline) {
        auto p = m_device->m_graphicsPipelinePool.get(m_currentGraphicsPipeline);
        wgpuRenderPassEncoderSetPipeline(m_renderPass, p->pipeline);
    }
}

inline void WebGPUCommandList::endRenderPass() {
    if (m_renderPass) {
        wgpuRenderPassEncoderEnd(m_renderPass);
        m_renderPass = nullptr;
    }
}

inline void WebGPUCommandList::setViewport(float x, float y, float w, float h, float minDepth, float maxDepth) {
    assert(m_renderPass && "[FATAL] WebGPUCommandList: setViewport requires an active render pass");
    wgpuRenderPassEncoderSetViewport(m_renderPass, x, y, w, h, minDepth, maxDepth);
}

inline void WebGPUCommandList::setScissor(float x, float y, float width, float height) {
    assert(m_renderPass && "[FATAL] WebGPUCommandList: setScissor requires an active render pass");
    wgpuRenderPassEncoderSetScissorRect(m_renderPass, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

inline void WebGPUCommandList::bindIndexBuffer(BufferHandle buffer, IndexType type, size_t offset) {
    assert(m_renderPass && "[FATAL] WebGPUCommandList: bindIndexBuffer requires an active render pass");
    auto b = m_device->m_bufferPool.get(buffer);
    wgpuRenderPassEncoderSetIndexBuffer(m_renderPass, b->buffer, detail::toWGPU(type), offset, b->size - offset);
}

inline void WebGPUCommandList::bindVertexBuffers(uint32_t firstBinding, std::span<const BufferHandle> buffers, std::span<const size_t> offsets) {
    assert(m_renderPass && "[FATAL] WebGPUCommandList: bindVertexBuffers requires an active render pass");
    assert(buffers.size() == offsets.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        auto b = m_device->m_bufferPool.get(buffers[i]);
        wgpuRenderPassEncoderSetVertexBuffer(m_renderPass, firstBinding + static_cast<uint32_t>(i), b->buffer, offsets[i], b->size - offsets[i]);
    }
}

inline void WebGPUCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    assert(m_renderPass && "[FATAL] WebGPUCommandList: draw requires an active render pass");
    if (m_currentGraphicsPipeline) {
        auto p = m_device->m_graphicsPipelinePool.get(m_currentGraphicsPipeline);
        wgpuRenderPassEncoderSetPipeline(m_renderPass, p->pipeline);
    }
    wgpuRenderPassEncoderDraw(m_renderPass, vertexCount, instanceCount, firstVertex, firstInstance);
}

inline void WebGPUCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    assert(m_renderPass && "[FATAL] WebGPUCommandList: drawIndexed requires an active render pass");
    if (m_currentGraphicsPipeline) {
        auto p = m_device->m_graphicsPipelinePool.get(m_currentGraphicsPipeline);
        wgpuRenderPassEncoderSetPipeline(m_renderPass, p->pipeline);
    }
    wgpuRenderPassEncoderDrawIndexed(m_renderPass, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

inline void WebGPUCommandList::ensureComputePass() {
    if (!m_computePass) {
        WGPUComputePassDescriptor d = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        m_computePass = wgpuCommandEncoderBeginComputePass(m_encoder, &d);
        if (m_currentComputePipeline) {
            auto p = m_device->m_computePipelinePool.get(m_currentComputePipeline);
            wgpuComputePassEncoderSetPipeline(m_computePass, p->pipeline);
        }
    }
}

inline WGPUDevice WebGPUCommandList::deviceWGPU() const {
    return m_device->deviceWGPU();
}

inline WebGPUDevice::WebGPUDevice(const DeviceInitInfo &init) {
    // Instance
    /*
    WGPUInstanceFeatureName feats[] = { WGPUInstanceFeatureName_TimedWaitAny };
    WGPUInstanceLimits il = {}; il.nextInChain = nullptr; il.timedWaitAnyMaxcount = 4;

    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
    id.requiredFeatureCount = 1;
    id.requiredFeatures = feats;
    id.requiredLimits = &il;
     */
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
    m_instance = wgpuCreateInstance(&id);

    // Surface
    m_surface = glfwCreateWindowWGPUSurface(m_instance, init.windowHandle->getGLFWwindow());

    // Adaptor
    WGPURequestAdapterOptions opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    opts.compatibleSurface = m_surface;
    opts.powerPreference = WGPUPowerPreference_HighPerformance; // not sure if this really makes a difference
    // ^^ most likely calls functions like forcing discrete GPU on vulkan?
    opts.backendType = detail::toWGPU(init.backend);
    opts.forceFallbackAdapter = false;
    opts.featureLevel = WGPUFeatureLevel_Core;

    struct AdapterCtx { WGPUAdapter adapter = nullptr; } actx;
    auto onAdapter = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
        WGPUStringView message, void* ud1, void* ud2) {
        if (status == WGPURequestAdapterStatus_Success) {
            static_cast<AdapterCtx*>(ud1)->adapter = adapter;
        }
        else {
            std::cerr << "WebGPU Adapter request failed: " << to_string_view(message) << std::endl;
        }
    };
    WGPURequestAdapterCallbackInfo aci{};
    aci.mode = WGPUCallbackMode_WaitAnyOnly;
    aci.callback = onAdapter;
    aci.userdata1 = &actx;

    // Must wait on adapter and device creation
    WGPUFuture aFuture = wgpuInstanceRequestAdapter(m_instance, &opts, aci);
    WGPUFutureWaitInfo aWait = WGPU_FUTURE_WAIT_INFO_INIT;
    aWait.future = aFuture;
    for (;;) {
        WGPUWaitStatus st = wgpuInstanceWaitAny(m_instance, 1, &aWait, 0);
        if (st == WGPUWaitStatus_Success) break;
        wgpuInstanceProcessEvents(m_instance); // flushes pending callbacks
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_adapter = actx.adapter;
    if (!m_adapter) {
        std::cerr << "WebGPU no compatible adapter found" << std::endl;
        return;
    }

    // Debug backend print
/*
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(m_adapter, &info) == WGPUStatus_Success) {
        fprintf(stderr, "Adapter: %.*s | Backend=%d | Vendor=0x%X Device=0x%X\n",
            (int)info.description.length, info.description.data ? info.description.data : "",
            (int)info.backendType, info.vendorID, info.deviceID);
    }
*/
    // Device
    WGPULimits supported = WGPU_LIMITS_INIT;
    wgpuAdapterGetLimits(m_adapter, &supported);

    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.label = WGPUStringView{ "blokDevice", 10 };
    dd.requiredLimits = &supported;

    // Device Lost Callback
    auto onLost = [](const WGPUDevice*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed) std::cerr << "WebGPU Device Lost (" <<
            std::to_string(static_cast<unsigned>(reason)) << "): " << to_string_view(message) << std::endl;
    };
    dd.deviceLostCallbackInfo = WGPU_DEVICE_LOST_CALLBACK_INFO_INIT;
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    dd.deviceLostCallbackInfo.callback = onLost;

    // Unhandled Errors Callback
    auto onError = [](const WGPUDevice* device, WGPUErrorType type, WGPUStringView message, void* ud1, void* ud2) {
        std::cerr << "WebGPU Error (" << std::to_string(static_cast<unsigned>(type)) << "): " << to_string_view(message) << std::endl;
    };
    dd.uncapturedErrorCallbackInfo = WGPU_UNCAPTURED_ERROR_CALLBACK_INFO_INIT;
    dd.uncapturedErrorCallbackInfo.callback = onError;
    dd.uncapturedErrorCallbackInfo.userdata1 = nullptr;

    WGPUDawnTogglesDescriptor toggles = WGPU_DAWN_TOGGLES_DESCRIPTOR_INIT;
    const char* enabled[] = { "use_dxc" };
    toggles.enabledToggles = enabled;
    toggles.enabledToggleCount = 1;
    toggles.chain.next = dd.nextInChain;
    dd.nextInChain = &toggles.chain;

    struct DeviceCtx { WGPUDevice device = nullptr; } dctx;
    auto onDevice = [](WGPURequestDeviceStatus status, WGPUDevice device,
        WGPUStringView message, void* ud1, void* ud2) {
        if (status == WGPURequestDeviceStatus_Success) {
            static_cast<DeviceCtx*>(ud1)->device = device;
        }
        else {
            std::cerr << "WebGPU Device request failed: " << to_string_view(message) << std::endl;
        }
    };
    WGPURequestDeviceCallbackInfo dci{};
    dci.mode = WGPUCallbackMode_WaitAnyOnly;
    dci.callback = onDevice;
    dci.userdata1 = &dctx;

    WGPUFuture dFuture = wgpuAdapterRequestDevice(m_adapter, &dd, dci);
    WGPUFutureWaitInfo dWait = WGPU_FUTURE_WAIT_INFO_INIT;
    dWait.future = dFuture;
    for (;;) {
        WGPUWaitStatus st = wgpuInstanceWaitAny(m_instance, 1, &dWait, 0);
        if (st == WGPUWaitStatus_Success) break;
        wgpuInstanceProcessEvents(m_instance);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_device = dctx.device;
    if (!m_device) {
        std::cerr << "WebGPU device creation failed." << std::endl;
    }

    // Logging
    auto onLog = [](WGPULoggingType type, WGPUStringView message, void* ud1, void* ud2) {
        std::cerr << "WebGPU Log (" << std::to_string(static_cast<unsigned>(type)) << "): " << to_string_view(message) << std::endl;
    };
    WGPULoggingCallbackInfo lci{};
    lci.callback = onLog;
    lci.userdata1 = nullptr;
    wgpuDeviceSetLoggingCallback(m_device, lci);

    // Capabilities
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps);

        // Texture Format
        // For now, I am hardsetting these
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm; // seems like a good default?
    if (init.backend == RenderBackend::WEBGPU_D3D12) format = WGPUTextureFormat_BGRA8Unorm;
    else if (init.backend == RenderBackend::WEBGPU_VULKAN) format = WGPUTextureFormat_RGBA8UnormSrgb;

        // Present Mode (Queried from init)
    WGPUPresentMode present = detail::toWGPU(init.presentMode);

        // Alpha (just do opaque for now)
    WGPUCompositeAlphaMode alpha = WGPUCompositeAlphaMode_Opaque; // trying this isntead
    // WGPUCompositeAlphaMode alpha = WGPUCompositeAlphaMode_Auto;
    /*
    for (size_t i = 0; i < caps.alphaModeCount; ++i) {
        if (caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque) {
            alpha = caps.alphaModes[i];
            break;
        }
    }
    */

    // TODO: fix this line
    WGPUTextureFormat viewFormats[] = { detail::toWGPU(detail::toSRGB(detail::fromWGPU(format))) };

    WGPUSurfaceConfiguration sc = WGPU_SURFACE_CONFIGURATION_INIT;
    sc.device = m_device;
    sc.format = format;
    sc.usage = WGPUTextureUsage_RenderAttachment;
    sc.presentMode = present;
    sc.alphaMode = alpha;
    sc.width = init.width;
    sc.height = init.height;
    // try view formats to fix sRGB issues
    sc.viewFormatCount = 1;
    sc.viewFormats = viewFormats;

    wgpuSurfaceConfigure(m_surface, &sc);
    m_surfaceConfigured = true;

    // Queue
    m_queue = wgpuDeviceGetQueue(m_device);

    // TODO: capabilties better
    m_deviceCapabilities.uniformBufferAlignment = 256;
    m_deviceCapabilities.storageBufferAlignment = 256;
    m_deviceCapabilities.maxPushConstantBytes = 0; // webgpu doesnt support
    m_deviceCapabilities.hasTimelineSemaphore = false;
    m_deviceCapabilities.hasExternalMemoryInterop = false;

    m_backbufferFormat = detail::toSRGB(detail::fromWGPU(format));
    m_presentMode = init.presentMode;
    m_framebufferHeight = init.height; m_framebufferWidth = init.width;
}

inline WebGPUDevice::~WebGPUDevice() {
    // (I am leaving notes on destruction order. Do not do anything other than listed in the steps beflow!)

    // Step 0: Make sure all GPU work is done and all transient objects are dropped (WGPUTexture, etc.)
    if (m_queue) { wgpuQueueRelease(m_queue); m_queue = nullptr; }

    // Step 1: Unconfigure Surface, then Release Surface
    if (m_surface && m_surfaceConfigured) { wgpuSurfaceUnconfigure(m_surface); m_surfaceConfigured = false; }
    if (m_surface) { wgpuSurfaceRelease(m_surface); m_surface = nullptr; }

    // Step 2: Destroy and Release the Device
    if (m_device) {
        //wgpuDeviceDestroy(m_device);
        wgpuDeviceRelease(m_device);
        m_device = nullptr;
    }

    // Step 3: Release the Adapter
    if (m_adapter) { wgpuAdapterRelease(m_adapter); m_adapter = nullptr; }

    // Step 4: Finally, Release the Instance
    if (m_instance) {
        // Wait for everything else to finish
        wgpuInstanceProcessEvents(m_instance);
        wgpuInstanceRelease(m_instance); m_instance = nullptr;
    }
}

inline BufferHandle WebGPUDevice::createBuffer(const BufferDescriptor & d, const void *initialData) {
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.usage = detail::toWGPU(d.usage);
    bd.size = d.size;
    bd.mappedAtCreation = initialData != nullptr;
    auto buf = wgpuDeviceCreateBuffer(m_device, &bd);

    if (initialData) {
        void* p = wgpuBufferGetMappedRange(buf, 0, d.size);
        std::memcpy(p, initialData, d.size);
        wgpuBufferUnmap(buf);
    }

    BufferRecord rec{};
    rec.buffer = buf;
    rec.size = d.size;
    return m_bufferPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyBuffer(BufferHandle h) {
    if (auto* r = m_bufferPool.get(h)) {
        if (r->buffer) {
            wgpuBufferDestroy(r->buffer);
            wgpuBufferRelease(r->buffer);
        }
        m_bufferPool.remove(h);
    }
}

inline ImageHandle WebGPUDevice::createImage(const ImageDescriptor & d, const void *initialPixels) {
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.dimension = detail::toWGPU(d.dimensions);
    td.size = { d.width, d.height, d.depth };
    td.mipLevelCount = d.mips;
    td.sampleCount = 1;
    td.format = detail::toWGPU(d.format);
    td.usage = detail::toWGPU(d.usage);
    auto tex = wgpuDeviceCreateTexture(m_device, &td);

    ImageRecord rec{};
    rec.texture = tex;
    rec.descriptor = d;
    auto handle = m_imagePool.add(std::move(rec));

    if (initialPixels) {
        // This assumes tightly packed upload
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tex;
        dst.mipLevel = 0;
        dst.origin = {0,0,0 };
        dst.aspect = detail::isDepthFormat(d.format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        WGPUExtent3D sz { d.width, d.height, d.depth };
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = detail::bytesPerPixel(d.format) * d.width;
        layout.rowsPerImage = d.height;
        WGPUTexelCopyBufferInfo bi = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        bi.buffer = nullptr;
        (void)bi;
        // I use QueueWriteTexture for somplicity
        wgpuQueueWriteTexture(m_queue, &dst, initialPixels,
            static_cast<size_t>(layout.bytesPerRow) * d.height * d.depth, &layout, &sz);
    }
    return handle;
}

inline void WebGPUDevice::destroyImage(ImageHandle h) {
    if (auto* r = m_imagePool.get(h)) {
        if (r->texture) {
            wgpuTextureDestroy(r->texture);
            wgpuTextureRelease(r->texture);
        }
        m_imagePool.remove(h);
    }
}

inline ImageViewHandle WebGPUDevice::createImageView(ImageHandle image, const ImageViewDescriptor & v) {
    auto* r = m_imagePool.get(image);
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    vd.baseMipLevel = v.baseMip;
    vd.mipLevelCount = v.mipCount;
    vd.baseArrayLayer = v.baseLayer;
    vd.arrayLayerCount = v.layerCount;
    vd.aspect = detail::isDepthFormat(r->descriptor.format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
    auto view = wgpuTextureCreateView(r->texture, &vd);
    ImageViewRecord rec{};
    rec.textureView = view;
    return m_imageViewPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyImageView(ImageViewHandle h) {
    if (auto* r = m_imageViewPool.get(h)) {
        if (r->textureView) {
            wgpuTextureViewRelease(r->textureView);
        }
        m_imageViewPool.remove(h);
    }
}

inline SamplerHandle WebGPUDevice::createSampler(const SamplerDescriptor & d) {
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sd.addressModeU = detail::toWGPU(d.addressU);
    sd.addressModeV = detail::toWGPU(d.addressV);
    sd.addressModeW = detail::toWGPU(d.addressW);
    sd.magFilter = detail::toWGPU(d.magFilter);
    sd.minFilter = detail::toWGPU(d.minFilter);
    sd.mipmapFilter = detail::toWGPU(d.mipFilter);
    sd.lodMinClamp = d.minLod;
    sd.lodMaxClamp = d.maxLod;
    if (d.compareEnable) {
        sd.compare = WGPUCompareFunction_LessEqual;
    }

    auto samp = wgpuDeviceCreateSampler(m_device, &sd);
    SamplerRecord rec{};
    rec.sampler = samp;
    return m_samplerPool.add(std::move(rec));
}

inline void WebGPUDevice::destroySampler(SamplerHandle h) {
    if (auto* r = m_samplerPool.get(h)) {
        if (r->sampler) {
            wgpuSamplerRelease(r->sampler);
        }
        m_samplerPool.remove(h);
    }
}

inline BindGroupLayoutHandle WebGPUDevice::createBindGroupLayout(const BindGroupLayoutDescriptor &d) {
    std::vector<WGPUBindGroupLayoutEntry> entries;
    entries.reserve(d.entries.size());
    for (auto& e : d.entries) {
        WGPUBindGroupLayoutEntry le = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        le.binding = e.binding;
        le.visibility = detail::toWGPU(e.visibleStages);
        le.bindingArraySize = 0;

        switch (e.type) {
        case BindingType::UNIFORMBUFFER:
            le.buffer.type = WGPUBufferBindingType_Uniform;
            le.buffer.hasDynamicOffset = false;
            le.buffer.minBindingSize = 0;
            break;
        case BindingType::STORAGEBUFFER:
            le.buffer.type = WGPUBufferBindingType_Storage;
            le.buffer.hasDynamicOffset = false;
            le.buffer.minBindingSize = 0;
            break;
        case BindingType::SAMPLEDIMAGE:
            le.texture.sampleType = WGPUTextureSampleType_Float;
            le.texture.viewDimension = WGPUTextureViewDimension_2D;
            le.texture.multisampled = false;
            break;
        case BindingType::STORAGEIMAGE:
            le.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            le.storageTexture.format = WGPUTextureFormat_RGBA8Unorm; //TODO: extend API to support formats
            le.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case BindingType::SAMPLER:
            le.sampler.type = WGPUSamplerBindingType_Filtering;
            break;
        }
        entries.push_back(le);
    }
    WGPUBindGroupLayoutDescriptor bld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bld.entryCount = static_cast<uint32_t>(entries.size());
    bld.entries = entries.data();
    auto bgl = wgpuDeviceCreateBindGroupLayout(m_device, &bld);
    BindGroupLayoutRecord rec{};
    rec.layout = bgl;
    rec.entries = d.entries;
    return m_bindGroupLayoutPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyBindGroupLayout(BindGroupLayoutHandle h) {
    if (auto* r = m_bindGroupLayoutPool.get(h)) {
        if (r->layout) {
            wgpuBindGroupLayoutRelease(r->layout);
        }
        m_bindGroupLayoutPool.remove(h);
    }
}

inline BindGroupHandle WebGPUDevice::createBindGroup(const BindGroupDescriptor &d) {
    auto* layoutRec = m_bindGroupLayoutPool.get(d.layout);
    std::vector<WGPUBindGroupEntry> entries;
    entries.reserve(d.entries.size());
    for (auto& e : d.entries) {
        WGPUBindGroupEntry be = WGPU_BIND_GROUP_ENTRY_INIT;
        be.binding = e.binding;
        switch (e.kind) {
        case IGPUDevice::BindGroupEntry::Kind::BUFFER: {
            auto* b = m_bufferPool.get(static_cast<BufferHandle>(e.handle));
            be.buffer = b->buffer;
            be.offset = e.offset;
            be.size = e.size ? e.size : (b->size - e.offset);
        }
            break;
        case IGPUDevice::BindGroupEntry::Kind::IMAGEVIEW: {
            auto* v = m_imageViewPool.get(static_cast<ImageViewHandle>(e.handle));
            be.textureView = v->textureView;
        }
            break;
        case IGPUDevice::BindGroupEntry::Kind::SAMPLER: {
            auto* s = m_samplerPool.get(static_cast<SamplerHandle>(e.handle));
            be.sampler = s->sampler;
        }
            break;
        }
        entries.push_back(be);
    }

    WGPUBindGroupDescriptor bgd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgd.layout = layoutRec->layout;
    bgd.entryCount = static_cast<uint32_t>(entries.size());
    bgd.entries = entries.data();
    auto bg = wgpuDeviceCreateBindGroup(m_device, &bgd);
    BindGroupRecord rec{};
    rec.bindGroup = bg;
    return m_bindGroupPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyBindGroup(BindGroupHandle h) {
    if (auto* r = m_bindGroupPool.get(h)) {
        if (r->bindGroup) {
            wgpuBindGroupRelease(r->bindGroup);
        }
        m_bindGroupPool.remove(h);
    }
}

inline PipelineLayoutHandle WebGPUDevice::createPipelineLayout(const PipelineLayoutDescriptor &d) {
    std::vector<WGPUBindGroupLayout> sets;
    sets.reserve(d.setLayouts.size());
    for (auto h : d.setLayouts) {
        sets.push_back(m_bindGroupLayoutPool.get(h)->layout);
    }

    WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pld.bindGroupLayoutCount = static_cast<uint32_t>(sets.size());
    pld.bindGroupLayouts = sets.data();
    pld.immediateSize = 0;

    auto pl = wgpuDeviceCreatePipelineLayout(m_device, &pld);
    PipelineLayoutRecord rec{};
    rec.layout = pl;
    return m_pipelineLayoutPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyPipelineLayout(PipelineLayoutHandle h) {
    if (auto* r = m_pipelineLayoutPool.get(h)) {
        if (r->layout) {
            wgpuPipelineLayoutRelease(r->layout);
        }
        m_pipelineLayoutPool.remove(h);
    }
}

inline ShaderModuleHandle WebGPUDevice::createShaderModule(const ShaderModuleDescriptor &d) {
    WGPUShaderModule sm{};
    WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;

    // TODO: When we make the core shader manager/compiler, refactor this
    if (d.ir == ShaderIR::SPIRV) {
        WGPUShaderSourceSPIRV spirv = WGPU_SHADER_SOURCE_SPIRV_INIT;
        spirv.code = reinterpret_cast<const uint32_t*>(d.bytes.data());
        spirv.codeSize = static_cast<uint32_t>(d.bytes.size() / 4);
        md.nextInChain = &spirv.chain;
    }
    else if (d.ir == ShaderIR::WGSL) {
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        WGPUStringView codeView{ reinterpret_cast<const char*>(d.bytes.data()), d.bytes.size() };
        wgsl.code = codeView;
        md.nextInChain = &wgsl.chain;
    }
    else {
        std::cerr << "[WARN] WebGPUDevice: GLSL or Unknown shader format not supported. " << std::endl;
        return 0;
    }
    sm = wgpuDeviceCreateShaderModule(m_device, &md);
    ShaderRecord rec{};
    rec.module = sm;
    rec.descriptor = d;
    rec.descriptor.bytes = {}; // avoids dangling span after returning
    return m_shaderPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyShaderModule(ShaderModuleHandle h) {
    if (auto* r = m_shaderPool.get(h)) {
        if (r->module) {
            wgpuShaderModuleRelease(r->module);
        }
        m_shaderPool.remove(h);
    }
}

inline GraphicsPipelineHandle WebGPUDevice::createGraphicsPipeline(const GraphicsPipelineDescriptor &d) {
    auto* vs = m_shaderPool.get(d.vs);
    auto* fs = m_shaderPool.get(d.fs);
    auto* pl = m_pipelineLayoutPool.get(d.pipelineLayout);

    // Vertex buffers/attributes
    uint32_t maxBinding = 0;
    for (auto& a : d.vertexInputs) maxBinding = std::max(maxBinding, a.binding);

    std::vector<std::vector<WGPUVertexAttribute>> attrs(maxBinding + 1);
    std::vector<uint32_t> strides(maxBinding + 1, 0);

    for (auto& a : d.vertexInputs) {
        WGPUVertexAttribute va = WGPU_VERTEX_ATTRIBUTE_INIT;
        va.shaderLocation = a.location;
        va.offset = a.offset; // note: ensure this is bytes
        va.format = detail::toWGPUVertexFormat(a.format);
        attrs[a.binding].push_back(va);
        strides[a.binding] = a.stride; // this one also ensure this is bytes
    }

    std::vector<WGPUVertexBufferLayout> vbl;
    for (uint32_t b = 0; b <= maxBinding; ++b) {
        if (attrs[b].empty()) continue;
        WGPUVertexBufferLayout l = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        l.arrayStride = strides[b];
        l.stepMode = WGPUVertexStepMode_Vertex;
        l.attributeCount = static_cast<uint32_t>(attrs[b].size());
        l.attributes = attrs[b].data();
        vbl.push_back(l);
    }

    // Primitive
    WGPUPrimitiveState prim = WGPU_PRIMITIVE_STATE_INIT;
    prim.topology = detail::toWGPU(d.primitiveTopology);
    prim.frontFace = detail::toWGPU(d.frontFace);
    prim.cullMode = detail::toWGPU(d.cull);

    // Depth-stencil
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPUDepthStencilState* pds = nullptr;
    if (d.depth.depthTest || d.depth.depthWrite) {
        ds.format = detail::toWGPU(d.depthFormat);
        ds.depthWriteEnabled = static_cast<WGPUOptionalBool>(d.depth.depthWrite);
        ds.depthCompare = d.depth.depthTest ? WGPUCompareFunction_LessEqual : WGPUCompareFunction_Always;
        pds = &ds;
    }

    // Color target
    WGPUBlendState bs = WGPU_BLEND_STATE_INIT;
    if (d.blend.enable) {
        bs.color.operation = WGPUBlendOperation_Add;
        bs.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        bs.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        bs.alpha.operation = WGPUBlendOperation_Add;
        bs.alpha.srcFactor = WGPUBlendFactor_One;
        bs.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    }
    WGPUColorTargetState cts = WGPU_COLOR_TARGET_STATE_INIT;
    // TODO; improve this
    cts.format = detail::toWGPU(detail::toSRGB(d.colorFormat));
    if (d.blend.enable) cts.blend = &bs;
    cts.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState = WGPU_FRAGMENT_STATE_INIT;
    if (fs) {
        fragState.module = fs->module;
        WGPUStringView fsEntry{ fs->descriptor.entryPoint.c_str(), fs->descriptor.entryPoint.size() };
        fragState.entryPoint = fsEntry;
        fragState.targetCount = 1;
        fragState.targets = &cts;
    }

    // Vertex stage
    WGPUVertexState vsState = WGPU_VERTEX_STATE_INIT;
    vsState.module = vs->module;
    WGPUStringView vsEntry { vs->descriptor.entryPoint.c_str(), vs->descriptor.entryPoint.size() };
    vsState.entryPoint = vsEntry;
    vsState.bufferCount = static_cast<uint32_t>(vbl.size());
    vsState.buffers = vbl.empty() ? nullptr : vbl.data();

    // Pipeline descriptor
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.layout = pl->layout;
    pd.vertex = vsState;
    pd.primitive = prim;
    pd.depthStencil = pds;
    pd.multisample = WGPU_MULTISAMPLE_STATE_INIT;
    pd.multisample.count = 1;
    pd.fragment = fs ? &fragState : nullptr;

    auto pipe = wgpuDeviceCreateRenderPipeline(m_device, &pd);
    GraphicsPipelineRecord rec{};
    rec.pipeline = pipe;
    return m_graphicsPipelinePool.add(std::move(rec));
}

inline void WebGPUDevice::destroyGraphicsPipeline(GraphicsPipelineHandle h) {
    if (auto* r = m_graphicsPipelinePool.get(h)) {
        if (r->pipeline) {
            wgpuRenderPipelineRelease(r->pipeline);
        }
        m_graphicsPipelinePool.remove(h);
    }
}

inline ComputePipelineHandle WebGPUDevice::createComputePipeline(const ComputePipelineDescriptor &d) {
    auto* cs = m_shaderPool.get(d.cs);
    auto* pl = m_pipelineLayoutPool.get(d.pipelineLayout);

    WGPUComputePipelineDescriptor cd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cd.layout = pl->layout;
    cd.compute = WGPU_COMPUTE_STATE_INIT;
    cd.compute.module = cs->module;
    WGPUStringView csEntry { cs->descriptor.entryPoint.c_str(), cs->descriptor.entryPoint.size() };
    cd.compute.entryPoint = csEntry;
    auto pipe = wgpuDeviceCreateComputePipeline(m_device, &cd);

    ComputePipelineRecord rec{};
    rec.pipeline = pipe;
    return m_computePipelinePool.add(std::move(rec));
}

inline void WebGPUDevice::destroyComputePipeline(ComputePipelineHandle h) {
    if (auto* r = m_computePipelinePool.get(h)) {
        if (r->pipeline) {
            wgpuComputePipelineRelease(r->pipeline);
        }
        m_computePipelinePool.remove(h);
    }
}

inline SwapchainHandle WebGPUDevice::createSwapchain(const SwapchainDescriptor & d) {
    // To be clear, this doesn't use swapchains (removed from wgpu) but Surface (which is kinda similar)
    if (!m_surface) return 0;

    SwapchainRecord rec{};
    rec.surface = m_surface;
    rec.configuration.device = m_device;
    rec.configuration.format = detail::toWGPU(d.format);
    rec.configuration.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
    rec.configuration.presentMode = detail::toWGPU(d.presentMode);
    rec.configuration.width = d.width;
    rec.configuration.height = d.height;
    rec.configuration.alphaMode = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(m_surface, &rec.configuration);
    return m_swapchainPool.add(std::move(rec));
}

inline void WebGPUDevice::destroySwapchain(SwapchainHandle h) {
    if (auto* r = m_swapchainPool.get(h)) {
        /*
        if (r->currentView) {
            wgpuTextureViewRelease(r->currentView);
            r->currentView = nullptr;
        }
        */ // Commented out for now, app owns view
        r->currentView = nullptr;
        if (r->currentTexture) {
            wgpuTextureRelease(r->currentTexture);
            r->currentTexture = nullptr;
        }
        // actual surface is owned by the device itself, so I don't destroy here.
        m_swapchainPool.remove(h);
    }
}

inline ImageViewHandle WebGPUDevice::acquireNextImage(SwapchainHandle h) {
    auto* r = m_swapchainPool.get(h);
    if (!r) return 0;
    // Commented out because, for now, the app owns the view
    /*
    if (r->currentView) {
        wgpuTextureViewRelease(r->currentView);
        r->currentView = nullptr;
    }
    */
    r->currentView = nullptr;

    if (r->currentTexture) {
        wgpuTextureRelease(r->currentTexture);
        r->currentTexture = nullptr;
    }

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(r->surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        // Try to recover by reconfiguring surface
        std::cerr << "acquireNextImage failed once... reconfiguring" << std::endl;
        wgpuSurfaceConfigure(r->surface, &r->configuration);
        wgpuSurfaceGetCurrentTexture(r->surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            std::cerr << "acquireNextImage failed twice." << std::endl;
            return 0;
        }
    }

    r->currentTexture = st.texture;
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;

    // TODO: pick the SRGB version of configured surface format (d3d12 fix)
    WGPUTextureFormat srgbViewFormat = detail::toWGPU(detail::toSRGB(m_backbufferFormat));
    vd.format = srgbViewFormat;
    vd.aspect = WGPUTextureAspect_All;
    r->currentView = wgpuTextureCreateView(st.texture, &vd);

    ImageViewRecord rec{};
    rec.textureView = r->currentView;
    return m_imageViewPool.add(std::move(rec));
}

inline void WebGPUDevice::present(SwapchainHandle h) {
    auto* r = m_swapchainPool.get(h);
    if (!r) return;
    wgpuSurfacePresent(r->surface);
    // TODO: The returned texture becomes invalid after Present. Although release at next acquire, I should make it so they cant call this more than once.
}

inline void *WebGPUDevice::mapBuffer(BufferHandle h, size_t offset, size_t size) {
    auto* r = m_bufferPool.get(h);
    if (!r) return nullptr;
    struct Ctx { bool done=false; } ctx;
    auto cb = [](WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2) {
        ((Ctx*)userdata1)->done = true;
    };

    WGPUBufferMapCallbackInfo info{};
    info.mode = WGPUCallbackMode_WaitAnyOnly;
    info.callback = cb;
    info.userdata1 = &ctx;
    info.userdata2 = nullptr;
    (void)wgpuBufferMapAsync(r->buffer, WGPUMapMode_Write, offset, size, info);
    while (!ctx.done) { /* spin */ }
    return wgpuBufferGetMappedRange(r->buffer, offset, size);
}

inline void WebGPUDevice::unmapBuffer(BufferHandle h) {
    auto* r = m_bufferPool.get(h);
    if (!r) return;
    wgpuBufferUnmap(r->buffer);
}

inline void WebGPUDevice::updateBuffer(BufferHandle h, size_t offset, size_t size, const void *data) {
    auto* r = m_bufferPool.get(h);
    if (!r) return;
    wgpuQueueWriteBuffer(m_queue, r->buffer, offset, data, size);
}

inline ICommandList *WebGPUDevice::createCommandList(QueueType q) {
    return new WebGPUCommandList(this, q);
}

inline void WebGPUDevice::destroyCommandList(ICommandList *c) {
    delete dynamic_cast<WebGPUCommandList*>(c);
}

inline void WebGPUDevice::submit(const SubmitBatch &batch) {
    std::vector<WGPUCommandBuffer> buffers{};
    buffers.reserve(batch.lists.size());
    for (auto* l : batch.lists) {
        auto* wl = dynamic_cast<WebGPUCommandList*>(l);
        buffers.push_back(wl->commandBuffer());
    }
    wgpuQueueSubmit(m_queue, buffers.size(), buffers.data());
}

inline void WebGPUDevice::waitIdle(QueueType q) {
    struct Ctx { bool done=false; } ctx;
    auto onDone = [](WGPUQueueWorkDoneStatus, WGPUStringView message, void* userdata1, void* userdata2) {
        ((Ctx*)userdata1)->done = true;
    };
    WGPUQueueWorkDoneCallbackInfo ci{};
    ci.mode = WGPUCallbackMode_WaitAnyOnly;
    ci.callback = onDone;
    ci.userdata1 = &ctx;
    ci.userdata2 = nullptr;
    (void)wgpuQueueOnSubmittedWorkDone(m_queue, ci);
    while (!ctx.done) { /* spin */ }
}

inline QueryPoolHandle WebGPUDevice::createTimestampQueryPool(uint32_t count) {
    WGPUQuerySetDescriptor qd = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    qd.type = WGPUQueryType_Timestamp;
    qd.count = count;
    auto qs = wgpuDeviceCreateQuerySet(m_device, &qd);

    QuerySetRecord rec{};
    rec.querySet = qs;
    rec.count = count;
    return m_querySetPool.add(std::move(rec));
}

inline void WebGPUDevice::destroyQueryPool(QueryPoolHandle h) {
    if (auto* r = m_querySetPool.get(h)) {
        if (r->querySet) {
            wgpuQuerySetRelease(r->querySet);
        }
        m_querySetPool.remove(h);
    }
}

inline bool WebGPUDevice::getQueryResults(QueryPoolHandle, uint32_t first, uint32_t count, std::span<uint64_t> outTimestampNs) {
    // TODO: Implement this
    // Note to self: WebGPU requires resolving queries to a buffer and then mapping.
    // Agnostic API doesnt currently model this. Add smth like ResolveQuery to ICommandList
    // and a staging buffer readback path
    return false;
}

}

#endif //BLOK_WEBGPU_DEVICE_HPP