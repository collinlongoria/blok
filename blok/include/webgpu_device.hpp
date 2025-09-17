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

#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu.hpp>

#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include "gpu_handles.hpp"

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

    struct TextureRec {
        wgpu::Texture tex;
        wgpu::TextureDescriptor desc;
        Format blokFormat{Format::UNKNOWN};
    };

    struct SwapchainRec {
        wgpu::SwapChain swapchain;
    };

    class WebGPUDevice;

    class WebGPUCommandList /*final*/ : public ICommandList {
    public:
        WebGPUCommandList(wgpu::Device* device, QueueType queue);
        ~WebGPUCommandList() override = default;

        void begin() override;
        void end() override;

        void resourceBarrierBatch(std::span<const ResourceBarrier> barriers) override;

        void copyBuffer(BufferHandle src, BufferHandle dst, std::span<const BufferCopyRegion> regions) override;
        void copyBufferToImage(BufferHandle src, ImageHandle dst, const ImageCopyRegion &region) override;
        void copyImageToBuffer(ImageHandle src, BufferHandle dst, const ImageCopyRegion &region) override;
        void copyImage(ImageHandle src, ImageHandle dst, const ImageCopyRegion &region) override;
        void clearBuffer(BufferHandle dst, size_t offset, size_t size, uint32_t value) override;
        void clearImage(ImageHandle dst, const ImageSubresourceRange &sub, const std::array<float, 4> &rgba) override;

        void bindGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        void bindComputePipeline(ComputePipelineHandle pipeline) override;
        void bindBindGroup(uint32_t setIndex, BindGroupHandle group) override;
        void pushConstants(uint32_t offsetBytes, uint32_t sizeBytes, const void *data) override;

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

        wgpu::CommandBuffer finishToCommandBuffer();

    private:
        WebGPUDevice* m_device{};
        QueueType m_queue{QueueType::GRAPHICS};
        wgpu::CommandEncoder m_commandEncoder{};
        wgpu::RenderPassEncoder m_renderPassEncoder{};
        wgpu::ComputePassEncoder m_computePassEncoder{};

        enum class PipelineType { NONE, GRAPHICS, COMPUTE };
        PipelineType m_pipelineType{PipelineType::NONE};

        wgpu::IndexFormat m_indexFormat{wgpu::IndexFormat::Undefined};
    };

    class WebGPUDevice /*final*/ : public IGPUDevice {
    public:
        WebGPUDevice() = default;
        explicit WebGPUDevice(wgpu::Device device);
        ~WebGPUDevice() override = default;

    private:
        DeviceInitInfo m_deviceInitInfo{};
        wgpu::Instance m_instance{};
        wgpu::Surface m_surface{};
        wgpu::Device m_device{};
        wgpu::Queue m_queue{};
        DeviceCapabilities m_capabilities{};



    };
}

#endif //BLOK_WEBGPU_DEVICE_HPP