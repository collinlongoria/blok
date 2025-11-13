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

#include "light.hpp"
#include "math.hpp"
#include "object.hpp"
#include "pipeline_system.hpp"
#include "GLFW/glfw3.h"

void vulkanFramebufferCallback(GLFWwindow* window, int width, int height);
extern bool resizeNeeded;

namespace blok {
class Window;

/*
 * These are the canonical structs for per-object and per-frame data. Update as needed
 */
struct alignas(16) FrameUBO {
    // Camera
    Matrix4 view;
    Matrix4 proj;

    float time = 0.0f;

    // Lights
    Light lights[MAX_LIGHTS];
    int lightCount = 0;
    Vector3 cameraPos{0,0,0};
};

struct ObjectUBO {
    glm::mat4 model;
};

struct alignas(16) GPUMaterial{
    alignas(16) Vector3 diffuse;
    alignas(16) Vector3 specular;
    alignas(16) Vector3 emission;
    alignas(16) float shininess;
};

// Push constant for raytracing
struct RayPC {

};

struct RayPayload {
    bool hit;
    Vector3 hitPos; // world coords of hit point
    int instanceIndex; // index of the object instance hit
    int primitiveIndex; // index of the hit triangle primitive within the object
    Vector3 bc; // barycentric coordinates of the hit point within the triangle
};

struct Buffer {
    vk::Buffer      handle{};
    VmaAllocation   alloc{};
    void*           mapped = nullptr;
    vk::DeviceSize  size = 0;
};

struct Image {
    vk::Image                   handle{};
    VmaAllocation               alloc{};
    vk::ImageView               view{};
    vk::Format                  format{vk::Format::eUndefined};
    uint32_t                    width = 0;
    uint32_t                    height = 0;
    uint32_t                    mipLevels = 1;
    uint32_t                    layers = 1;
    vk::SampleCountFlagBits     samples{vk::SampleCountFlagBits::e1};
    vk::ImageLayout             currentLayout{vk::ImageLayout::eUndefined};
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
    vk::CommandPool cmdPool{};
    vk::CommandBuffer cmd{};

    // Uniforms
    Buffer globalUBO{};
    vk::DeviceSize uboHead = 0;
    vk::DescriptorSet frameSet{};
    vk::DescriptorSet computeFrameSet{};
};

// Obj file metadata
struct ModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials;
    std::vector<int32_t> matIdx;
    std::vector<std::string> textures;

    bool readAssimpFile(const std::string& path, const Matrix4& M);
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

    // Objects, Meshes, Materials
    MeshBuffers uploadMesh(const void* vertices, vk::DeviceSize bytes, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount);
    void setRenderList(const std::vector<Object>* list);

    Image loadTexture2D(const std::string& path, bool generateMips = true);
    MeshBuffers loadMeshOBJ(const std::string& path);
    std::vector<Object> initObjectsFromMesh(const std::string& pipelineName, const std::string& meshPath);
    void initObjectFromMesh(Object& obj, const std::string& pipelineName, const std::string& meshPath);

    // Lights
    int addLight(const Light& light);
    void removeLight(int id);
    [[nodiscard]] std::span<const Light> lights() const { return m_lights; }

    // Descriptor Materials
    void buildMaterialSetForObject(Object& obj, const std::string& texturePath);

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

    // GUI
    void initGUI();

    // Upload
    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO, bool mapped = false);
    void uploadToBuffer(const void* src, vk::DeviceSize size, Buffer& dst, vk::DeviceSize dstOffset = 0);
    void copyBuffer(Buffer& src, Buffer& dst, vk::DeviceSize size);

    Image createImage(uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageTiling tiling = vk::ImageTiling::eOptimal, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1, uint32_t mipLevels = 1, uint32_t layers = 1, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO);
    void copyBufferToImage(vk::CommandBuffer cmd, Buffer& staging, Image& img, uint32_t w, uint32_t h, uint32_t baseLayer = 0, uint32_t layerCount = 1);
    void generateMipmaps(vk::CommandBuffer cmd, Image& img);

    vk::Buffer uploadVertexBuffer(const void* data, vk::DeviceSize sizeBytes, uint32_t vertexCount);
    vk::Buffer uploadIndexBuffer(const uint32_t* data, uint32_t indexCount);

    // Dynamic rendering helpers
    void beginRendering(vk::CommandBuffer cmd, vk::ImageView colorView, vk::ImageView depthView, vk::Extent2D extent, const std::array<float,4>& clearColor, float clearDepth = 1.0f, uint32_t clearStencil = 0);
    void endRendering(vk::CommandBuffer cmd);

    // Utilities
    [[nodiscard]] vk::Format findDepthFormat() const;
    [[nodiscard]] vk::SampleCountFlagBits getMaxUsableSampleCount() const;

    // Recording
    void recordGraphicsCommands(uint32_t imageIndex, const Camera& cam, const Scene& scene);

    // Cleanup and Recreation
    void cleanupSwapChain();
    void recreateSwapChain();

    std::vector<const char*> getRequiredExtensions();
    std::vector<const char*> getRequiredDeviceExtensions();

    // Small helper
    inline vk::DeviceSize alignUp(vk::DeviceSize v, vk::DeviceSize a) {
        return (v + (a - 1)) & ~(a - 1);
    }

    // Raytracing
    void initRayTracing();
    void

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
    std::unique_ptr<PipelineSystem> m_pipelineSystem;

    // Descriptor Stuff
    DescriptorAllocatorGrowable m_descAlloc;
    vk::DescriptorPool m_imguiDescriptorPool{}; // for imgui

    // Named resources
    std::unordered_map<std::string, Image>   m_images;
    std::unordered_map<std::string, Buffer>  m_buffers;
    std::unordered_map<std::string, Sampler> m_samplers;
    std::unordered_map<std::string, MeshBuffers> m_meshes;
    std::vector<Buffer> m_ownedBuffers;

    // Objects
    const std::vector<Object>* m_renderList = nullptr;

    // Lights
    std::vector<Light> m_lights;
    std::vector<uint32_t> m_freeLights;

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

    // Raytracing
    float m_maxAnis = 0;
    RayPC m_pcRay{};
    int m_num_atrous_iterations = 5;
    uint32_t handleSize{};
    uint32_t handleAlignment{};
    uint32_t baseAlignment{};

};
}

#endif //BLOK_VULKAN_RENDERER_HPP