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

#include <webgpu.h>

#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include "gpu_handles.hpp"
#include "gpu_types.hpp"
#include "gpu_types.hpp"

namespace blok {
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
    const T* get(uint64_t h) const {
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
        case Format::R8_UINT: return WGPUTextureFormat_R8Uint;
        case Format::R16_UINT: return WGPUTextureFormat_R16Uint;
        case Format::R32_UINT: return WGPUTextureFormat_R32Uint;
        case Format::RGBA32_UINT: return WGPUTextureFormat_RGBA32Uint;
        case Format::R16_FLOAT: return WGPUTextureFormat_R16Float;
        case Format::R32_FLOAT: return WGPUTextureFormat_R32Float;
        case Format::RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
        case Format::RGBA32_FLOAT: return WGPUTextureFormat_RGBA32Float;
        case Format::D24S8: return WGPUTextureFormat_Depth24PlusStencil8;
        case Format::D32_FLOAT: return WGPUTextureFormat_Depth32Float;
        }
        return WGPUTextureFormat_Undefined;
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
        case Format::R8_UNORM: return WGPUVertexFormat_Uint8;
        case Format::RG8_UNORM: return WGPUVertexFormat_Uint8x2;
        case Format::RGBA8_UNORM: return WGPUVertexFormat_Uint8x4;
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
    WGPUDevice deviceWGPU() const;
};

/*
 * Internal resource records
 */
class WebGPUDevice final : public IGPUDevice {
public:
    explicit WebGPUDevice(const DeviceInitInfo& init);
    ~WebGPUDevice() override;

    DeviceCapabilities getCapabilities() const override;
private:
    friend class WebGPUCommandList;

    WGPUInstance m_instance{};
    WGPUAdapter m_adapter{};
    WGPUDevice m_device{};
    WGPUQueue m_queue{};
    WGPUSurface m_surface{};

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
}



}

#endif //BLOK_WEBGPU_DEVICE_HPP