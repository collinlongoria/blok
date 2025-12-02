/*
* File: renderer.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef RENDERER_HPP
#define RENDERER_HPP
#include "vulkan_context.hpp"
#define GLFW_INCLUDE_NONE
#include <memory>
#include <optional>
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include "camera.hpp"
#include "descriptors.hpp"
#include "renderer_raytracing.hpp"
#include "resources.hpp"
#include "shader_manager.hpp"

namespace blok {

void framebufferResizeCallback(GLFWwindow* window, int width, int height);
extern bool resizeNeeded;

class Renderer {
public:
    explicit Renderer(int width, int height);
    ~Renderer();

    void render(const Camera& c, float dt);

    [[nodiscard]]
    GLFWwindow* getWindow() const { return m_window; }

    // TODO will probably remove this function and refactor this later
    void addWorld(WorldSvoGpu& gpuWorld) {
        m_world = &gpuWorld;

        // Upload all SVO buffers
        uploadSvoBuffers(*m_world);

        // Build BLAS/TLAS
        buildChunkBlas(*m_world);
        buildChunkTlas(*m_world);

        // Update descriptor sets
        m_raytracer.updateDescriptorSet(*m_world);
    }
    void updateWorld() {
        if (!m_world) return;

        m_device.waitIdle();

        // Reupload SVO buffers
        uploadSvoBuffers(*m_world);

        // Rebuild BLAS/TLAS
        buildChunkBlas(*m_world);
        buildChunkTlas(*m_world);

        // Update descriptor sets
        m_raytracer.updateDescriptorSet(*m_world);
    }
    void cleanupWorld(WorldSvoGpu& gpuWorld);

    // gui
    void updatePerformanceData(float fps, float ms);

private:
    // Device creation
    void createWindow();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void chooseSurfaceFormatAndPresentMode();
    void createLogicalDevice();
    void createAllocator();
    void createSwapChain();
    [[nodiscard]]
    vk::Format findDepthFormat() const;
    [[nodiscard]]
    vk::SampleCountFlagBits getMaxUsableSampleCount() const;
    void createImageResources();
    void createCommandPoolAndBuffers();
    void createSyncObjects();
    void createPerFrameUniforms();
    void queryRayTracingProperties();

    // gui
    void createGui();
    void destroyGui();
    void renderPerformanceData();

    // Upload
    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO, bool mapped = false);
    void uploadToBuffer(const void* src, vk::DeviceSize size, Buffer& dst, vk::DeviceSize dstOffset = 0);
    void copyBuffer(Buffer& src, Buffer& dst, vk::DeviceSize size);

    Image createImage(uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageTiling tiling = vk::ImageTiling::eOptimal, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1, uint32_t mipLevels = 1, uint32_t layers = 1, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO);
    void copyBufferToImage(vk::CommandBuffer cmd, Buffer& staging, Image& img, uint32_t w, uint32_t h, uint32_t baseLayer = 0, uint32_t layerCount = 1);
    void generateMipmaps(vk::CommandBuffer cmd, Image& img);

    vk::Buffer uploadVertexBuffer(const void* data, vk::DeviceSize sizeBytes, uint32_t vertexCount);
    vk::Buffer uploadIndexBuffer(const uint32_t* data, uint32_t indexCount);

    void uploadSvoBuffers(WorldSvoGpu& gpuWorld);

    // Rendering
    void beginFrame();
    void drawFrame(const Camera& c, float dt);
    void cmdBeginRendering(vk::CommandBuffer cmd, vk::ImageView colorView, vk::ImageView depthView, vk::Extent2D extent, const std::array<float,4>& clearColor, float clearDepth = 1.0f, uint32_t clearStencil = 0);
    void cmdEndRendering(vk::CommandBuffer cmd);
    void endFrame();

    // Raytracing
    vk::AccelerationStructureKHR buildChunkBlas(WorldSvoGpu& gpuWorld);
    vk::AccelerationStructureKHR buildChunkTlas(WorldSvoGpu& gpuWorld);

    // Cleanup and Recreation
    void cleanupSwapChain();
    void recreateSwapChain();

    // Extension helpers
    std::vector<const char*> getRequiredExtensions();
    std::vector<const char*> getRequiredDeviceExtensions();

    inline vk::DeviceSize alignUp(vk::DeviceSize v, vk::DeviceSize a) {
        return (v + (a - 1)) & ~(a - 1);
    }

private:
    int m_width = 800, m_height = 600;
    GLFWwindow* m_window = nullptr;

    vk::Instance m_instance{};
    vk::SurfaceKHR m_surface{};
    vk::PhysicalDevice m_physicalDevice{};
    vk::Device m_device{};
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphics;
        std::optional<uint32_t> present;
        std::optional<uint32_t> compute;
        bool complete() const { return graphics && present && compute; }
    } m_qfi;
    vk::Queue m_graphicsQueue{};
    vk::Queue m_presentQueue{};
    vk::Queue m_computeQueue{};

    VmaAllocator m_allocator = nullptr;

    vk::SwapchainKHR m_swapchain{};
    std::vector<vk::Image> m_swapImages{};
    std::vector<vk::ImageView> m_swapViews{};
    vk::Format m_colorFormat{vk::Format::eB8G8R8A8Unorm};
    vk::Format m_depthFormat{vk::Format::eD32Sfloat};
    vk::Format m_outputFormat {vk::Format::eR32G32B32A32Sfloat};
    vk::Extent2D m_swapExtent{};
    vk::ColorSpaceKHR m_colorSpace{vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::PresentModeKHR m_presentMode{vk::PresentModeKHR::eMailbox};
    bool m_swapchainDirty = false;

    std::vector<vk::Semaphore> m_presentSignals;
    std::vector<vk::Fence> m_imagesInFlight;
    std::vector<vk::ImageLayout> m_swapImageLayouts;

    // Rendering resources
    Image m_depth{};
    Image m_outputImage{};

    DescriptorAllocatorGrowable m_descAlloc;
    vk::DescriptorPool m_guiDescriptorPool{};

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_frameIndex = 0;
    std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> m_frames{};

    vk::CommandPool m_uploadPool{};
    vk::CommandBuffer m_uploadCmd{};
    vk::Fence m_uploadFence{};

    ShaderManager m_shaderManager;

    WorldSvoGpu* m_world = nullptr;

    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProps{};
    RayTracing m_raytracer;
    uint32_t m_frameCount = 0;
    friend class RayTracing;
};

}

#endif //RENDERER_HPP