/*
* File: vulkan_renderer
* Project: blok
* Author: Collin Longoria
* Created on: 10/7/2025
*
* Description:
*/
#ifndef BLOK_VULKAN_RENDERER_HPP
#define BLOK_VULKAN_RENDERER_HPP

#include <optional>

#include "renderer.hpp"

#include "vulkan/vulkan.hpp"
#include <vk_mem_alloc.h>

#include "pipeline_system.hpp"


namespace blok {
class Window;

struct Buffer {
    vk::Buffer handle{};
    VmaAllocation alloc{};
    VmaAllocationInfo allocInfo{};
    vk::DeviceSize size = 0;
    vk::DeviceSize alignment = 0;
    vk::BufferUsageFlags usage{};
    bool mapped = false;
    void* mappedPtr = nullptr;
};

struct Image {
    vk::Image handle{};
    VmaAllocation alloc{};
    VmaAllocationInfo allocInfo{};
    vk::ImageView view{};
    vk::Format format{};
    vk::Extent3D extent{};
    vk::ImageLayout currentLayout{vk::ImageLayout::eUndefined};
    uint32_t mipLevels = 1;
    uint32_t layers = 1;
    vk::SampleCountFlagBits sampled{vk::SampleCountFlagBits::e1};
};

struct Sampler {
    vk::Sampler handle{};
};

// Scene stuff, may move.
enum class ResourceKind {
    Buffer,
    StorageImage,
    SampledImage,
    CombinedImageSampler
};

struct PassResource {
    uint32_t set;
    uint32_t binding;
    ResourceKind kind;
    std::string name;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

struct PassResources {
    std::vector<PassResource> items;
};

struct ScenePass {
    std::string pipelineName;
    bool isCompute = false;
    uint32_t groupsX=0,groupsY=0,groupsZ=0; // for compute
    uint32_t vertexCount=0; // for graphics
    PassResources resources; // descriptors requested by the pass
    std::vector<std::byte> pushConstantBytes;
};

struct VulkanScene { std::vector<ScenePass> passes; };

struct FrameResources {
    // Sync
    vk::Semaphore imageAvailable{};
    vk::Semaphore renderFinished{};
    vk::Fence inFlight{};

    // Commands
    vk::CommandPool cmdPool{};
    vk::CommandBuffer cmd{};

    // Uniforms
    Buffer globalUBO{};
    vk::DeviceSize uboHead = 0;
};

class VulkanRenderer final : public Renderer {
public:
    explicit VulkanRenderer(Window* window);
    ~VulkanRenderer() override;

    void init() override;

    void beginFrame() override;
    void drawFrame(const Camera &cam, const Scene &scene) override;
    void endFrame() override;

    [[nodiscard]] vk::Format getColorFormat() const { return m_colorFormat; }

    void shutdown() override;

private: // functions
    // Initialization
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void chooseSurfaceFormatAndPresentMode();
    void createLogicalDevice();

    // VMA
    void createAllocator();
    void destroyAllocator();

    // Swapchain and Attachments
    void createSwapChain();
    void createDepthResources();

    // Commands and Sync
    void createCommandPoolAndBuffers();
    void createSyncObjects();

    // Per frame UBOs
    void createPerFrameUniforms();

    // Upload
    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO, bool mapped = false);
    void uploadToBuffer(const void* src, vk::DeviceSize size, Buffer& dst, vk::DeviceSize dstOffset = 0);
    void copyBuffer(Buffer& src, Buffer& dst, vk::DeviceSize size);

    Image createImage(uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageTiling tiling = vk::ImageTiling::eOptimal, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1, uint32_t mipLevels = 1, uint32_t layers = 1, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO);
    void transitionImage(Image& img, vk::ImageLayout newLayout, vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess, vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, uint32_t baseMip = 0, uint32_t levelCount = VK_REMAINING_MIP_LEVELS, uint32_t baseLayer = 0, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);
    void copyBufferToImage(Buffer& staging, Image& img, uint32_t w, uint32_t h, uint32_t baseLayer = 0, uint32_t layerCount = 1);
    void generateMipmaps(Image& img);

    // Dynamic rendering helpers
    void beginRendering(vk::CommandBuffer cmd, vk::ImageView colorView, vk::ImageView depthView, vk::Extent2D extent, const std::array<float,4>& clearColor, float clearDepth = 1.0f, uint32_t clearStencil = 0);
    void endRendering(vk::CommandBuffer cmd);

    // Utilities
    [[nodiscard]] vk::Format findDepthFormat() const;
    [[nodiscard]] vk::SampleCountFlagBits getMaxUsableSampleCount() const;

    // Recording
    void recordGraphicsCommands(uint32_t imageIndex, const Camera& cam, const VulkanScene& scene);

    // Cleanup and Recreation
    void cleanupSwapChain();
    void recreateSwapChain();

    std::vector<const char*> getRequiredExtensions();
    std::vector<const char*> getRequiredDeviceExtensions();

private: // resources
    std::shared_ptr<Window> m_window = nullptr;

    // Core
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

    // vma
    VmaAllocator m_allocator = nullptr;

    // SwapChain
    vk::SwapchainKHR m_swapchain{};
    std::vector<vk::Image> m_swapImages{};
    std::vector<vk::ImageView> m_swapViews{};
    vk::Format m_colorFormat{vk::Format::eB8G8R8A8Unorm};
    vk::Format m_depthFormat{vk::Format::eD32Sfloat};
    vk::Extent2D m_swapExtent{};
    vk::ColorSpaceKHR m_colorSpace{vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::PresentModeKHR m_presentMode{vk::PresentModeKHR::eMailbox};

    std::vector<vk::Semaphore> m_presentSignals;
    std::vector<vk::Fence> m_imagesInFlight;
    std::vector<vk::ImageLayout> m_swapImageLayouts;

    // Depth attachment
    Image m_depth{};

    // Systems
    std::unique_ptr<ShaderSystem> m_shaderSystem;
    std::unique_ptr<DescriptorSystem> m_descriptorSystem;
    std::unique_ptr<PipelineSystem> m_pipelineSystem;

    // Named resources
    std::unordered_map<std::string, Image>   m_images;
    std::unordered_map<std::string, Buffer>  m_buffers;
    std::unordered_map<std::string, Sampler> m_samplers;

    // Per-frame
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_frameIndex = 0;
    std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> m_frames{};

    // Transient upload
    vk::CommandPool m_uploadPool{};
    vk::CommandBuffer m_uploadCmd{};
    vk::Fence m_uploadFence{};

    // Flags
    bool m_swapchainDirty = false;
};
}

#endif //BLOK_VULKAN_RENDERER_HPP