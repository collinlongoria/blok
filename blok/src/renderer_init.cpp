/*
* File: renderer_init.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include <iostream>
#include <set>

#include "renderer.hpp"

namespace blok {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)messageSeverity;
    (void)pUserData;
    (void)messageType;
    std::cerr << "[Vulkan API] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height), m_shaderManager(m_device), m_raytracer(this), m_temporal(this) {
    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    createWindow();
    createInstance();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
    createSurface();
    pickPhysicalDevice();
    chooseSurfaceFormatAndPresentMode();
    createLogicalDevice();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);
    createAllocator();

    createSwapChain();
    createImageResources();

    createCommandPoolAndBuffers();
    createSyncObjects();
    createPerFrameUniforms();

    DescriptorAllocatorGrowable::PoolSizeRatio ratios[] = {
        {vk::DescriptorType::eUniformBuffer, 4.0f},
        {vk::DescriptorType::eUniformBufferDynamic, 1.0f},
        {vk::DescriptorType::eCombinedImageSampler, 4.0f},
        {vk::DescriptorType::eStorageBuffer, 2.0f},
        {vk::DescriptorType::eAccelerationStructureKHR, 1.0f},
        {vk::DescriptorType::eSampledImage, 2.0f},
        {vk::DescriptorType::eStorageImage, 1.0f},
    };
    m_descAlloc.init(m_device, 512, std::span{ratios, std::size(ratios)});

    m_shaderManager = ShaderManager(m_device);

    m_raytracer.createDescriptorSetLayout();
    m_raytracer.allocateDescriptorSet();

    queryRayTracingProperties();

    m_raytracer.createPipeline();
    m_raytracer.createSBT();

    m_temporal.init(m_swapExtent.width, m_swapExtent.height);

    createGui();
}

Renderer::~Renderer() {
    glfwDestroyWindow(m_window);
    glfwTerminate();

    if (!m_device) return;
    m_device.waitIdle();

    destroyGui();

    if (m_world) {
        cleanupWorld(*m_world);
        m_world = nullptr;
    }

    // per frame resources
    for (auto& fr : m_frames) {
        if (fr.cmdPool) { m_device.destroyCommandPool(fr.cmdPool); }
        if (fr.imageAvailable) { m_device.destroySemaphore(fr.imageAvailable); }
        if (fr.renderFinished) { m_device.destroySemaphore(fr.renderFinished); }
        if (fr.inFlight) { m_device.destroyFence(fr.inFlight); }
        if (fr.frameUBO.handle) { vmaDestroyBuffer(m_allocator, fr.frameUBO.handle, fr.frameUBO.alloc); }
    }

    if (m_uploadFence) { m_device.destroyFence(m_uploadFence); }
    if (m_uploadCmd) { m_device.freeCommandBuffers(m_uploadPool, 1, &m_uploadCmd); }
    if (m_uploadPool) { m_device.destroyCommandPool(m_uploadPool); }

    if (m_raytracer.rtSetLayout) { m_device.destroyDescriptorSetLayout(m_raytracer.rtSetLayout); }
    if (m_raytracer.rtPipeline.layout) { m_device.destroyPipelineLayout(m_raytracer.rtPipeline.layout); }
    if (m_raytracer.rtPipeline.pipeline) { m_device.destroyPipeline(m_raytracer.rtPipeline.pipeline); }

    if (m_raytracer.rtPipeline.rgenSBT.handle) { vmaDestroyBuffer(m_allocator, m_raytracer.rtPipeline.rgenSBT.handle, m_raytracer.rtPipeline.rgenSBT.alloc); }
    if (m_raytracer.rtPipeline.hitSBT.handle) { vmaDestroyBuffer(m_allocator, m_raytracer.rtPipeline.hitSBT.handle, m_raytracer.rtPipeline.hitSBT.alloc); }
    if (m_raytracer.rtPipeline.missSBT.handle) { vmaDestroyBuffer(m_allocator, m_raytracer.rtPipeline.missSBT.handle, m_raytracer.rtPipeline.missSBT.alloc); }

    m_temporal.cleanup();

    m_descAlloc.destroyPools(m_device);

    cleanupSwapChain();

    if (m_allocator) vmaDestroyAllocator(m_allocator);
    m_allocator = nullptr;

    if (m_device) m_device.destroy();
    if (m_surface) m_instance.destroySurfaceKHR(m_surface);
    if (m_instance) m_instance.destroy();
}

void Renderer::cleanupWorld(WorldSvoGpu& gpuWorld) {
    // Wait for GPU to finish any pending work
    m_device.waitIdle();

    // Destroy TLAS and its resources
    if (gpuWorld.tlas.handle && gpuWorld.tlas.handle != VK_NULL_HANDLE) {
        m_device.destroyAccelerationStructureKHR(gpuWorld.tlas.handle);
        gpuWorld.tlas.handle = nullptr;
    }
    if (gpuWorld.tlas.buffer.handle && gpuWorld.tlas.buffer.alloc) {
        vmaDestroyBuffer(m_allocator, gpuWorld.tlas.buffer.handle, gpuWorld.tlas.buffer.alloc);
        gpuWorld.tlas.buffer = {};
    }
    if (gpuWorld.tlasInstanceBuffer.handle && gpuWorld.tlasInstanceBuffer.alloc) {
        vmaDestroyBuffer(m_allocator, gpuWorld.tlasInstanceBuffer.handle, gpuWorld.tlasInstanceBuffer.alloc);
        gpuWorld.tlasInstanceBuffer = {};
    }

    // Destroy BLAS and its resources
    if (gpuWorld.blas.handle && gpuWorld.blas.handle != VK_NULL_HANDLE) {
        m_device.destroyAccelerationStructureKHR(gpuWorld.blas.handle);
        gpuWorld.blas.handle = nullptr;
    }
    if (gpuWorld.blas.buffer.handle && gpuWorld.blas.buffer.alloc) {
        vmaDestroyBuffer(m_allocator, gpuWorld.blas.buffer.handle, gpuWorld.blas.buffer.alloc);
        gpuWorld.blas.buffer = {};
    }
    if (gpuWorld.blasAabbBuffer.handle && gpuWorld.blasAabbBuffer.alloc) {
        vmaDestroyBuffer(m_allocator, gpuWorld.blasAabbBuffer.handle, gpuWorld.blasAabbBuffer.alloc);
        gpuWorld.blasAabbBuffer = {};
    }

    // Destroy SVO and chunk buffers
    if (gpuWorld.svoBuffer.handle && gpuWorld.svoBuffer.alloc) {
        vmaDestroyBuffer(m_allocator, gpuWorld.svoBuffer.handle, gpuWorld.svoBuffer.alloc);
        gpuWorld.svoBuffer = {};
    }
    if (gpuWorld.chunkBuffer.handle && gpuWorld.chunkBuffer.alloc) {
        vmaDestroyBuffer(m_allocator, gpuWorld.chunkBuffer.handle, gpuWorld.chunkBuffer.alloc);
        gpuWorld.chunkBuffer = {};
    }
}

void Renderer::createWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(m_width, m_height, "Blok!", nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

void Renderer::createInstance() {
    // app info
    vk::ApplicationInfo app{};
    app.pApplicationName = "SVO Test";
    app.applicationVersion = VK_MAKE_API_VERSION(0,1,0,0);
    app.pEngineName = "SVO Test";
    app.engineVersion = VK_MAKE_API_VERSION(0,1,0,0);
    app.apiVersion = VK_API_VERSION_1_4;

    auto exts = getRequiredExtensions();

    vk::InstanceCreateInfo ici{};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();

#ifndef NDEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = layers;

    VkDebugUtilsMessengerCreateInfoEXT dbg{};
    dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = debugCallback;
    ici.pNext = &dbg;
#endif

    m_instance = vk::createInstance(ici);
}

void Renderer::createSurface() {
    VkSurfaceKHR raw = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(static_cast<VkInstance>(m_instance), m_window, nullptr, &raw) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API failed to create Vulkan surface via GLFW!");
    }
    m_surface = raw;
}

void Renderer::pickPhysicalDevice() {
    auto phys = m_instance.enumeratePhysicalDevices();
    if (phys.empty()) throw std::runtime_error("Vulkan API failed to find supported devices!");

    auto deviceExtensions = getRequiredDeviceExtensions();

    auto supportAllExts = [&](vk::PhysicalDevice pd) {
        auto exts = pd.enumerateDeviceExtensionProperties();
        std::set<std::string> want(deviceExtensions.begin(), deviceExtensions.end());
        for (auto& e : exts) want.erase(e.extensionName);
        return want.empty();
    };

    auto findQFI = [&](vk::PhysicalDevice pd) {
        QueueFamilyIndices out{};
        auto qf = pd.getQueueFamilyProperties();
        for (uint32_t i = 0; i < qf.size(); ++i) {
            const auto& p = qf[i];
            if (!out.graphics && (p.queueFlags & vk::QueueFlagBits::eGraphics)) out.graphics = i;
            if (!out.compute && (p.queueFlags & vk::QueueFlagBits::eCompute)) out.compute = i;
            if (!out.present) {
                VkBool32 presentSupport = VK_FALSE;
                auto r = pd.getSurfaceSupportKHR(i, m_surface, &presentSupport);
                if (presentSupport) out.present = i;
            }
        }
        return out;
    };

    // just choosing first device
    vk::PhysicalDevice best{};
    for (auto& pd : phys) {
        if (!supportAllExts(pd)) continue;
        auto qfi = findQFI(pd);
        if (!qfi.complete()) continue;
        auto props = pd.getProperties();
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            m_physicalDevice = pd; m_qfi = qfi; return;
        }
        if (!best) { best = pd; m_qfi = qfi; }
    }
    if (!m_physicalDevice) m_physicalDevice = best; // if couldnt find perfect, get next best.
    if (!m_physicalDevice) throw std::runtime_error("Vulkan API failed to pick a suitable device!");
}

void Renderer::chooseSurfaceFormatAndPresentMode() {
    auto formats = m_physicalDevice.getSurfaceFormatsKHR(m_surface);
    auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(m_surface);

    // format
    vk::SurfaceFormatKHR chosen{};
    for (auto& f : formats) {
        if ((f.format == vk::Format::eB8G8R8A8Unorm || f.format == vk::Format::eR8G8B8A8Unorm) &&
        f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) { chosen = f; break; }
    }
    if (chosen.format == vk::Format::eUndefined) chosen = formats.front();

    m_colorFormat = chosen.format;
    m_colorSpace = chosen.colorSpace;

    // present mode
    m_presentMode = vk::PresentModeKHR::eFifo;
    for (auto pm : presentModes) {
        if (pm == vk::PresentModeKHR::eMailbox) { m_presentMode = pm; break; }
    }
}

void Renderer::createLogicalDevice() {
    std::vector<vk::DeviceQueueCreateInfo> qcis;
    std::set<uint32_t> unique;
    unique.insert(*m_qfi.graphics);
    unique.insert(*m_qfi.present);
    unique.insert(*m_qfi.compute);

    float priority = 1.0f;
    for (auto idx : unique) {
        vk::DeviceQueueCreateInfo qci{};
        qci.queueFamilyIndex = idx;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);
    }

    auto devExts = getRequiredDeviceExtensions();

    vk::PhysicalDeviceVulkan13Features f13{};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;

    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accel{};
    accel.accelerationStructure = VK_TRUE;

    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rt{};
    rt.rayTracingPipeline = VK_TRUE;

    vk::PhysicalDeviceBufferDeviceAddressFeatures bda{};
    bda.bufferDeviceAddress = VK_TRUE;

    bda.pNext = &accel;
    accel.pNext = &rt;
    rt.pNext = &f13;

    vk::DeviceCreateInfo dci{};
    dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = static_cast<uint32_t>(devExts.size());
    dci.ppEnabledExtensionNames = devExts.data();
    dci.pNext = &bda;

#ifndef NDEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    dci.enabledLayerCount = 1; dci.ppEnabledLayerNames = layers;
#endif

    m_device = m_physicalDevice.createDevice(dci);

    m_graphicsQueue = m_device.getQueue(*m_qfi.graphics, 0);
    m_presentQueue = m_device.getQueue(*m_qfi.present, 0);
    m_computeQueue = m_device.getQueue(*m_qfi.compute, 0);
}

void Renderer::createAllocator() {
    VmaAllocatorCreateInfo ci{};
    ci.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    ci.physicalDevice = static_cast<VkPhysicalDevice>(m_physicalDevice);
    ci.device = static_cast<VkDevice>(m_device);
    ci.instance = static_cast<VkInstance>(m_instance);
    ci.vulkanApiVersion = VK_API_VERSION_1_4;
    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API failed to create VMA!");
    }
}

void Renderer::createSwapChain() {
        auto caps = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);

    uint32_t fbw = 0, fbh = 0;
    glfwGetFramebufferSize(m_window, reinterpret_cast<int*>(&fbw), reinterpret_cast<int*>(&fbh));

    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        m_swapExtent = caps.currentExtent;
    }
    else {
        m_swapExtent = vk::Extent2D{
            std::clamp(fbw, caps.minImageExtent.width,  caps.maxImageExtent.width),
            std::clamp(fbh, caps.minImageExtent.height, caps.maxImageExtent.height)
        };
    }

    uint32_t imageCount = std::min(caps.maxImageCount ? caps.maxImageCount : UINT32_MAX,
        std::max(caps.minImageCount + 1, 2u));

    vk::SwapchainCreateInfoKHR sci{};
    sci.surface = m_surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = m_colorFormat;
    sci.imageColorSpace = m_colorSpace;
    sci.imageExtent = vk::Extent2D{ m_swapExtent.width, m_swapExtent.height };
    sci.imageArrayLayers = 1;
    sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

    uint32_t qfi[2] = { *m_qfi.graphics, *m_qfi.present };
    if (*m_qfi.graphics != *m_qfi.present) {
        sci.imageSharingMode = vk::SharingMode::eConcurrent;
        sci.queueFamilyIndexCount = 2; sci.pQueueFamilyIndices = qfi;
    }
    else {
        sci.imageSharingMode = vk::SharingMode::eExclusive;
    }

    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    sci.presentMode = m_presentMode;
    sci.clipped = VK_TRUE;

    m_swapchain = m_device.createSwapchainKHR(sci);
    m_swapImages = m_device.getSwapchainImagesKHR(m_swapchain);

    // views
    m_swapViews.clear();
    m_swapViews.reserve(m_swapImages.size());
    for (auto img : m_swapImages) {
        vk::ImageViewCreateInfo ivci{};
        ivci.image = img;
        ivci.viewType = vk::ImageViewType::e2D;
        ivci.format = m_colorFormat;
        ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        ivci.subresourceRange.baseMipLevel = 0; ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0; ivci.subresourceRange.layerCount = 1;
        m_swapViews.push_back(m_device.createImageView(ivci));
    }

    // Per image sync
    for (auto s : m_presentSignals) if (s) m_device.destroySemaphore(s);
    m_presentSignals.clear();
    m_presentSignals.resize(m_swapImages.size());
    for (auto &s : m_presentSignals) { s = m_device.createSemaphore({}); }

    m_imagesInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
    m_swapImageLayouts.assign(m_swapImages.size(), vk::ImageLayout::eUndefined);
}

vk::Format Renderer::findDepthFormat() const {
    const std::array<vk::Format, 3> candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD24UnormS8Uint,
        vk::Format::eD16Unorm
    };

    for (auto f : candidates) {
        auto props = m_physicalDevice.getFormatProperties(f);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return f;
        }
    }
    return vk::Format::eD32Sfloat;
}

vk::SampleCountFlagBits Renderer::getMaxUsableSampleCount() const {
    auto props = m_physicalDevice.getProperties().limits;
    vk::SampleCountFlags counts = props.framebufferColorSampleCounts & props.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e8) return vk::SampleCountFlagBits::e8;
    if (counts & vk::SampleCountFlagBits::e4) return vk::SampleCountFlagBits::e4;
    if (counts & vk::SampleCountFlagBits::e2) return vk::SampleCountFlagBits::e2;
    return vk::SampleCountFlagBits::e1;
}

void Renderer::createImageResources() {
    m_depthFormat = findDepthFormat();

    // getMaxUsableSampleCount()
    constexpr vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

    m_depth = createImage(m_swapExtent.width, m_swapExtent.height, m_depthFormat,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        vk::ImageTiling::eOptimal, samples, 1, 1, VMA_MEMORY_USAGE_AUTO);

    m_outputImage = createImage(m_swapExtent.width, m_swapExtent.height, m_outputFormat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
        vk::ImageTiling::eOptimal, samples, 1, 1, VMA_MEMORY_USAGE_AUTO);
}

void Renderer::createCommandPoolAndBuffers() {
    // per frame pools/buffers
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& fr = m_frames[i];
        vk::CommandPoolCreateInfo pci{};
        pci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pci.queueFamilyIndex = *m_qfi.graphics;
        fr.cmdPool = m_device.createCommandPool(pci);

        vk::CommandBufferAllocateInfo cai{};
        cai.commandPool = fr.cmdPool;
        cai.level = vk::CommandBufferLevel::ePrimary;
        cai.commandBufferCount = 1;
        fr.cmd = m_device.allocateCommandBuffers(cai).front();
    }

    // Upload
    vk::CommandPoolCreateInfo upci{};
    upci.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    upci.queueFamilyIndex = *m_qfi.graphics;
    m_uploadPool = m_device.createCommandPool(upci);

    vk::CommandBufferAllocateInfo cai{};
    cai.commandPool = m_uploadPool;
    cai.level = vk::CommandBufferLevel::ePrimary;
    cai.commandBufferCount = 1;
    m_uploadCmd = m_device.allocateCommandBuffers(cai).front();

    vk::FenceCreateInfo fci{};
    m_uploadFence = m_device.createFence(fci);
}

void Renderer::createSyncObjects() {
    for (auto& fr : m_frames) {
        vk::SemaphoreCreateInfo si{};
        fr.imageAvailable = m_device.createSemaphore(si);
        fr.renderFinished = m_device.createSemaphore(si);

        vk::FenceCreateInfo fi{};
        fi.flags = vk::FenceCreateFlagBits::eSignaled;
        fr.inFlight = m_device.createFence(fi);
    }
}

void Renderer::createPerFrameUniforms() {
    constexpr vk::DeviceSize defaultUBOSize = 64ull * 1024ull;
    for (auto& fr : m_frames) {
        fr.frameUBO = createBuffer(defaultUBOSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true
            );
    }
}

void Renderer::queryRayTracingProperties() {
    vk::PhysicalDeviceProperties2 props2{};
    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
    props2.pNext = &rtProps;

    m_physicalDevice.getProperties2(&props2);

    m_rtProps = rtProps;
}

void Renderer::cleanupSwapChain() {
    if (m_depth.view)
        m_device.destroyImageView(m_depth.view);

    if (m_depth.handle) {
        vmaDestroyImage(m_allocator, m_depth.handle, m_depth.alloc);
    }
    m_depth = {};

    if (m_outputImage.view)
        m_device.destroyImageView(m_outputImage.view);

    if (m_outputImage.handle) {
        vmaDestroyImage(m_allocator, m_outputImage.handle, m_outputImage.alloc);
    }
    m_outputImage = {};

    for (auto v : m_swapViews) if (v) m_device.destroyImageView(v);
    m_swapViews.clear();

    if (m_swapchain) m_device.destroySwapchainKHR(m_swapchain);
    m_swapchain = vk::SwapchainKHR{};
    m_swapImages.clear();

    for (auto s : m_presentSignals) if (s) m_device.destroySemaphore(s);
    m_presentSignals.clear();
    m_imagesInFlight.clear();
    m_swapImageLayouts.clear();
}

void Renderer::recreateSwapChain() {
    // wait for the window to be non-zerp (i.e not minimized)
    int w = 0, h = 0; do { glfwGetFramebufferSize(m_window, &w, &h); glfwWaitEventsTimeout(0.016); } while (w == 0 && h == 0);

    m_device.waitIdle();
    cleanupSwapChain();
    createSwapChain();
    createImageResources();

    // resize temporal buffers too
    m_temporal.resize(m_swapExtent.width, m_swapExtent.height);
}

std::vector<const char *> Renderer::getRequiredExtensions() {
    uint32_t count = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> out(glfwExts, glfwExts + count);
#ifndef NDEBUG
    out.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    return out;
}

std::vector<const char *> Renderer::getRequiredDeviceExtensions() {
    std::vector<const char*> out = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, /* Graphics */
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, /* Dynamic Rendering (isnt this in core now?) */
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, /* TODO: no clue, figure out what specific this one is needed for */
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME /* Raytracing Pipeline */
    };
    return out;
}

}