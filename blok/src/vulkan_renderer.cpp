/*
* File: vulkan_renderer
* Project: blok
* Author: Collin Longoria
* Created on: 10/7/2025
*
* Description:
*/

#include "vulkan_renderer.hpp"

#include <iostream>
#include <ostream>

#define GLFW_INCLUDE_NONE
#include <set>

#include "shader_pipe.hpp"
#include "GLFW/glfw3.h"

#include "window.hpp"

namespace blok {

// per swapchain image signaling for present and image-in-flight
static std::vector<vk::Semaphore> g_presentSignals;
static std::vector<vk::Fence> g_imagesInFlight;
static std::vector<vk::ImageLayout> g_swapImageLayouts;
static float timer = 0.0f;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)messageSeverity;
    (void)pUserData;
    (void)messageType;
    std::cerr << "[Vulkan API] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

static bool isColorFormat(vk::Format fmt) {
    switch (fmt) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
    case vk::Format::eS8Uint:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return false;
    default: return true;
    }
}

static vk::ImageAspectFlags aspectFromFormat(vk::Format fmt) {
    switch (fmt) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
        return vk::ImageAspectFlagBits::eDepth;
    case vk::Format::eS8Uint:
        return vk::ImageAspectFlagBits::eStencil;
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    default:
        return vk::ImageAspectFlagBits::eColor;
    }
}

VulkanRenderer::VulkanRenderer(Window* window) {
    m_window = std::shared_ptr<Window>(window, [](Window*){});
}

VulkanRenderer::~VulkanRenderer() {
    shutdown();
}

void VulkanRenderer::init() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    chooseSurfaceFormatAndPresentMode();
    createLogicalDevice();
    createAllocator();

    createSwapChain();
    createDepthResources();

    createCommandPoolAndBuffers();
    createSyncObjects();

    createDescriptorSetLayouts();
    createDescriptorPools();
    createPerFrameUniforms();
    allocatePerFrameDescriptorSets();

    createPipelineCache();
    createFullscreenQuadBuffers();
    createRayTarget();
    // default sampler
    vk::SamplerCreateInfo si{};
    si.magFilter = vk::Filter::eLinear;
    si.minFilter = vk::Filter::eLinear;
    si.mipmapMode = vk::SamplerMipmapMode::eLinear;
    si.addressModeU = vk::SamplerAddressMode::eRepeat;
    si.addressModeV = vk::SamplerAddressMode::eRepeat;
    si.addressModeW = vk::SamplerAddressMode::eRepeat;
    si.maxAnisotropy = 1.0f;
    m_defaultSampler.handle = m_device.createSampler(si);
    allocateMaterialAndComputeDescriptorSets();
    createComputePipeline();
    createGraphicsPipeline();



    std::cout << "Vulkan API initialized!"  << std::endl;
}

void VulkanRenderer::beginFrame() {
    FrameResources& fr = m_frames[m_frameIndex];
    // wait for previous work
    if (fr.inFlight) {
        (void)m_device.waitForFences(fr.inFlight, VK_TRUE, UINT64_MAX);
        m_device.resetFences(fr.inFlight);
    }
    if (fr.cmdPool)
        m_device.resetCommandPool(fr.cmdPool);
}

void VulkanRenderer::drawFrame(Camera &cam, const Scene &scene) {
    FrameResources& fr = m_frames[m_frameIndex];

    // acquire
    uint32_t imageIndex = 0;
    vk::Result acq = m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX, fr.imageAvailable, {}, &imageIndex);
    if (acq == vk::Result::eErrorOutOfDateKHR) { m_swapchainDirty = true; return; }
    if (acq == vk::Result::eSuboptimalKHR) { m_swapchainDirty = true; }

    // ensure swapchain image not in flight
    if (imageIndex < g_imagesInFlight.size() && g_imagesInFlight[imageIndex]) {
        (void)m_device.waitForFences(g_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    if (imageIndex >= g_imagesInFlight.size()) {
        g_imagesInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
    }
    g_imagesInFlight[imageIndex] = fr.inFlight;

    // record
    vk::CommandBufferBeginInfo bi{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
    fr.cmd.begin(bi);
    recordGraphicsCommands(imageIndex, {}, {}); // currently just clears
    fr.cmd.end();

    // submit
    vk::SemaphoreSubmitInfo waitSem{};
    waitSem.semaphore = fr.imageAvailable;
    waitSem.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

    vk::Semaphore presentSignal = g_presentSignals[imageIndex];

    vk::SemaphoreSubmitInfo signalSem{};
    signalSem.semaphore = presentSignal;
    signalSem.stageMask = vk::PipelineStageFlagBits2::eNone;

    vk::CommandBufferSubmitInfo cbsi{};
    cbsi.commandBuffer = fr.cmd;

    vk::SubmitInfo2 submit{};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitSem;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cbsi;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalSem;

    auto r = m_graphicsQueue.submit2(1, &submit, fr.inFlight);

    // present
    vk::PresentInfoKHR pi{};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &presentSignal;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &imageIndex;
    vk::Result pres = m_presentQueue.presentKHR(pi);
    if (pres == vk::Result::eErrorOutOfDateKHR || pres == vk::Result::eSuboptimalKHR) {
        m_swapchainDirty = true;
    }
}

void VulkanRenderer::endFrame() {
    // advance frame
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

    if (m_swapchainDirty) {
        recreateSwapChain();
        m_swapchainDirty = false;
    }
}

void VulkanRenderer::shutdown() {
    if (!m_device) return;

    m_device.waitIdle();

    // per frame resources
    for (auto& fr : m_frames) {
        if (fr.globalUBO.handle) { vmaDestroyBuffer(m_allocator, fr.globalUBO.handle, fr.globalUBO.alloc); }
        if (fr.objectUBO.handle) { vmaDestroyBuffer(m_allocator, fr.objectUBO.handle, fr.objectUBO.alloc); }
        if (fr.cmdPool) { m_device.destroyCommandPool(fr.cmdPool); }
        if (fr.imageAvailable) { m_device.destroySemaphore(fr.imageAvailable); }
        if (fr.renderFinished) { m_device.destroySemaphore(fr.renderFinished); }
        if (fr.inFlight) { m_device.destroyFence(fr.inFlight); }
    }

    if (m_uploadFence) { m_device.destroyFence(m_uploadFence); }
    if (m_uploadCmd) { m_device.freeCommandBuffers(m_uploadPool, 1, &m_uploadCmd); }
    if (m_uploadPool) { m_device.destroyCommandPool(m_uploadPool); }

    if (m_defaultSampler.handle) { m_device.destroySampler(m_defaultSampler.handle); }

    // pipeline and device
    if (m_gfx.handle) { m_device.destroyPipeline(m_gfx.handle); }
    if (m_gfx.layout) { m_device.destroyPipelineLayout(m_gfx.layout); }
    if (m_pipelineCache) { m_device.destroyPipelineCache(m_pipelineCache); }

    if (m_descLayouts.global) { m_device.destroyDescriptorSetLayout(m_descLayouts.global); }
    if (m_descLayouts.material) { m_device.destroyDescriptorSetLayout(m_descLayouts.material); }
    if (m_descLayouts.object) { m_device.destroyDescriptorSetLayout(m_descLayouts.object); }
    if (m_descLayouts.compute) { m_device.destroyDescriptorSetLayout(m_descLayouts.compute); }

    if (m_descPools.pool) { m_device.destroyDescriptorPool(m_descPools.pool); }

    cleanupSwapChain();

    // Destroy custom GPU resources
    if (m_rayImage.view)  m_device.destroyImageView(m_rayImage.view);
    if (m_rayImage.handle) vmaDestroyImage(m_allocator, m_rayImage.handle, m_rayImage.alloc);
    if (m_fsVBO.handle)    vmaDestroyBuffer(m_allocator, m_fsVBO.handle, m_fsVBO.alloc);
    if (m_fsIBO.handle)    vmaDestroyBuffer(m_allocator, m_fsIBO.handle, m_fsIBO.alloc);


    destroyAllocator();

    if (m_device) m_device.destroy();
    if (m_surface) m_instance.destroySurfaceKHR(m_surface);
    if (m_instance) m_instance.destroy();
}

void VulkanRenderer::createInstance() {
    // app info
    vk::ApplicationInfo app{};
    app.pApplicationName = "blok";
    app.applicationVersion = VK_MAKE_API_VERSION(0,1,0,0);
    app.pEngineName = "blok";
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

void VulkanRenderer::createSurface() {
    GLFWwindow* win = nullptr;
    if (m_window) win = m_window->getGLFWwindow();
    else throw std::runtime_error("Vulkan API did not receive a window!");

    VkSurfaceKHR raw = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(static_cast<VkInstance>(m_instance), win, nullptr, &raw) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API failed to create Vulkan surface via GLFW!");
    }
    m_surface = raw;
}

void VulkanRenderer::pickPhysicalDevice() {
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

void VulkanRenderer::chooseSurfaceFormatAndPresentMode() {
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

void VulkanRenderer::createLogicalDevice() {
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

    vk::DeviceCreateInfo dci{};
    dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = static_cast<uint32_t>(devExts.size());
    dci.ppEnabledExtensionNames = devExts.data();
    dci.pNext = &f13;

#ifndef NDEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    dci.enabledLayerCount = 1; dci.ppEnabledLayerNames = layers;
#endif

    m_device = m_physicalDevice.createDevice(dci);

    m_graphicsQueue = m_device.getQueue(*m_qfi.graphics, 0);
    m_presentQueue = m_device.getQueue(*m_qfi.present, 0);
    m_computeQueue = m_device.getQueue(*m_qfi.compute, 0);
}

void VulkanRenderer::createAllocator() {
    VmaAllocatorCreateInfo ci{};
    ci.flags = 0;
    ci.physicalDevice = static_cast<VkPhysicalDevice>(m_physicalDevice);
    ci.device = static_cast<VkDevice>(m_device);
    ci.instance = static_cast<VkInstance>(m_instance);
    ci.vulkanApiVersion = VK_API_VERSION_1_4;
    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API failed to create VMA!");
    }
}

void VulkanRenderer::destroyAllocator() {
    if (m_allocator) vmaDestroyAllocator(m_allocator);
    m_allocator = nullptr;
}

void VulkanRenderer::createSwapChain() {
    auto caps = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);

    uint32_t fbw = 0, fbh = 0;
    GLFWwindow* win = m_window ? m_window->getGLFWwindow() : nullptr;
    glfwGetFramebufferSize(win, reinterpret_cast<int*>(&fbw), reinterpret_cast<int*>(&fbh));

    if (caps.currentExtent.width == 0 != std::numeric_limits<uint32_t>::max()) {
        m_swapExtent = caps.currentExtent;
    }
    else {
        m_swapExtent = vk::Extent2D{ std::clamp(fbw, caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(fbh, caps.minImageExtent.height, caps.maxImageExtent.height) };
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
    for (auto s : g_presentSignals) if (s) m_device.destroySemaphore(s);
    g_presentSignals.clear();
    g_presentSignals.resize(m_swapImages.size());
    for (auto &s : g_presentSignals) { s = m_device.createSemaphore({}); }

    g_imagesInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
    g_swapImageLayouts.assign(m_swapImages.size(), vk::ImageLayout::eUndefined);
}

void VulkanRenderer::createDepthResources() {
    m_depthFormat = findDepthFormat();

    // getMaxUsableSampleCount()
    constexpr vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

    m_depth = createImage(m_swapExtent.width, m_swapExtent.height, m_depthFormat,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        vk::ImageTiling::eOptimal, samples, 1, 1, VMA_MEMORY_USAGE_AUTO);
}

Shader VulkanRenderer::createShaderModule(const std::vector<uint32_t> &code) const {
    vk::ShaderModuleCreateInfo ci{};
    ci.codeSize = code.size() * sizeof(uint32_t);
    ci.pCode = code.data();
    Shader s{};
    s.module = m_device.createShaderModule(ci);
    return s;
}

void VulkanRenderer::createGraphicsPipeline() {
    // Compile shaders (assumed provided)
    // glsl_to_spriv(path, stage) -> std::vector<uint8_t>
    auto vsBytes = shaderpipe::glsl_to_spirv(shaderpipe::load_shader_file("assets/shaders/fullscreen.vert.glsl"), shaderpipe::ShaderStage::VERTEX, shaderpipe::VKVersion::VK_1_4);
    auto fsBytes = shaderpipe::glsl_to_spirv(shaderpipe::load_shader_file("assets/shaders/fullscreen.frag.glsl"), shaderpipe::ShaderStage::FRAGMENT, shaderpipe::VKVersion::VK_1_4);
    auto vs = createShaderModule(vsBytes);
    auto fs = createShaderModule(fsBytes);

    vk::PipelineShaderStageCreateInfo stages[2] = {
        { {}, vk::ShaderStageFlagBits::eVertex,   vs.module, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, fs.module, "main" }
    };

    // Vertex layout: vec2 pos (loc 0), vec2 uv (loc 1)
    vk::VertexInputBindingDescription bind{ 0, 4 * sizeof(float), vk::VertexInputRate::eVertex };
    std::array<vk::VertexInputAttributeDescription,2> attrs = {{
        {0, 0, vk::Format::eR32G32Sfloat,            0},
        {1, 0, vk::Format::eR32G32Sfloat, 2*sizeof(float)}
    }};
    vk::PipelineVertexInputStateCreateInfo vi{ {}, 1, &bind, static_cast<uint32_t>(attrs.size()), attrs.data() };

    vk::PipelineInputAssemblyStateCreateInfo ia{ {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE };

    vk::Viewport vp{}; // dynamic
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(m_swapExtent.width);
    vp.height   = static_cast<float>(m_swapExtent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    vk::Rect2D sc{};
    sc.offset = vk::Offset2D{0, 0};
    sc.extent = m_swapExtent;

    vk::PipelineViewportStateCreateInfo vpState{};
    vpState.viewportCount = 1;              // must be > 0
    vpState.pViewports    = &vp;            // static viewport
    vpState.scissorCount  = 1;              // must be > 0
    vpState.pScissors     = &sc;

    vk::PipelineRasterizationStateCreateInfo rs{};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo ms{}; // sampleCount implicitly 1 when dynamic rendering uses 1

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable  = VK_FALSE; // fullscreen pass; depth clear is fine
    ds.depthWriteEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo cb{ {}, VK_FALSE, vk::LogicOp::eClear, 1, &cba };

    std::array<vk::DynamicState,2> dyn{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynCI{ {}, static_cast<uint32_t>(dyn.size()), dyn.data() };

    // Pipeline layout: set0 global, set1 object, set2 material
    std::array<vk::DescriptorSetLayout,3> sets = { m_descLayouts.global, m_descLayouts.object, m_descLayouts.material };
    vk::PipelineLayoutCreateInfo pl{};
    pl.setLayoutCount = static_cast<uint32_t>(sets.size());
    pl.pSetLayouts = sets.data();
    m_gfx.layout = m_device.createPipelineLayout(pl);

    // Dynamic rendering formats (+ sample count = 1)
    vk::PipelineRenderingCreateInfo renderCI{};
    renderCI.colorAttachmentCount = 1;
    renderCI.pColorAttachmentFormats = &m_colorFormat;
    renderCI.depthAttachmentFormat   = m_depthFormat;

    vk::GraphicsPipelineCreateInfo gp{};
    gp.pNext = &renderCI;
    gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vpState;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dynCI;
    gp.layout = m_gfx.layout;

    m_gfx.handle = m_device.createGraphicsPipeline(m_pipelineCache, gp).value;

    // Clean up shader modules
    m_device.destroyShaderModule(vs.module);
    m_device.destroyShaderModule(fs.module);
}

void VulkanRenderer::createComputePipeline() {
    auto csBytes = shaderpipe::glsl_to_spirv(shaderpipe::load_shader_file("assets/shaders/raytrace.comp.glsl"), shaderpipe::ShaderStage::COMPUTE, shaderpipe::VKVersion::VK_1_4);
    auto cs = createShaderModule(csBytes);

    vk::DescriptorSetLayout computeOnly[] = { m_descLayouts.compute };

    vk::PushConstantRange pcr{};
    pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcr.offset     = 0;
    pcr.size       = sizeof(ComputePC);

    vk::PipelineLayoutCreateInfo pl{};
    pl.setLayoutCount = 1;
    pl.pSetLayouts    = computeOnly;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &pcr;
    m_compute.layout = m_device.createPipelineLayout(pl);

    vk::ComputePipelineCreateInfo ci{};
    ci.stage  = { {}, vk::ShaderStageFlagBits::eCompute, cs.module, "main" };
    ci.layout = m_compute.layout;
    m_compute.handle = m_device.createComputePipeline(m_pipelineCache, ci).value;

    m_device.destroyShaderModule(cs.module);
}


void VulkanRenderer::createPipelineCache() {
    vk::PipelineCacheCreateInfo ci{};
    m_pipelineCache = m_device.createPipelineCache(ci);
}

void VulkanRenderer::createDescriptorSetLayouts() {
    // set = 0 : global UBO @ binding 0
    vk::DescriptorSetLayoutBinding g0{};
    g0.binding = 0;
    g0.descriptorType = vk::DescriptorType::eUniformBuffer;
    g0.descriptorCount = 1;
    g0.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo gci{};
    gci.bindingCount = 1;
    gci.pBindings = &g0;
    m_descLayouts.global = m_device.createDescriptorSetLayout(gci);

    // set = 1 : object UBO @ binding 0
    vk::DescriptorSetLayoutBinding o0{};
    o0.binding = 0;
    o0.descriptorType = vk::DescriptorType::eUniformBuffer;
    o0.descriptorCount = 1;
    o0.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo oci{};
    oci.bindingCount = 1;
    oci.pBindings = &o0;
    m_descLayouts.object = m_device.createDescriptorSetLayout(oci);

    // set = 2 : material (combined image sampler @ binding 0)
    vk::DescriptorSetLayoutBinding m0{};
    m0.binding = 0;
    m0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    m0.descriptorCount = 1;
    m0.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo mci{};
    mci.bindingCount = 1;
    mci.pBindings = &m0;
    m_descLayouts.material = m_device.createDescriptorSetLayout(mci);

    // set = 3 : compute (storage image @ binding 0)
    vk::DescriptorSetLayoutBinding c0{};
    c0.binding         = 0;
    c0.descriptorType  = vk::DescriptorType::eStorageImage;
    c0.descriptorCount = 1;
    c0.stageFlags      = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo cci{};
    cci.bindingCount = 1;
    cci.pBindings    = &c0;
    m_descLayouts.compute = m_device.createDescriptorSetLayout(cci);
}

void VulkanRenderer::createDescriptorPools() {
    // How many sets do we make per frame?
    // set0: global UBO
    // set1: object UBO
    // set2: material (combined image sampler)
    // set3: compute  (storage image)
    const uint32_t setsPerFrame = 4;

    // Headroom for anything else that might grab sets from the same pool
    // (e.g., transient materials, imgui, etc.)
    const uint32_t extraSets = 16;

    // Descriptor counts per frame (match what you're writing)
    const uint32_t ubosPerFrame      = 2; // global + object
    const uint32_t samplersPerFrame  = 1; // material combined image sampler
    const uint32_t storageImgPerFrame= 1; // compute storage image

    const uint32_t frames = MAX_FRAMES_IN_FLIGHT;

    // OVERSIZE THESE to avoid VK_ERROR_OUT_OF_POOL_MEMORY
    const uint32_t totalUBOs        = ubosPerFrame       * frames + 16;   // + headroom
    const uint32_t totalCombinedImg = samplersPerFrame   * frames + 16;
    const uint32_t totalStorageImg  = storageImgPerFrame * frames + 16;

    std::array<vk::DescriptorPoolSize, 3> sizes = {
        vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer,        totalUBOs        },
        vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, totalCombinedImg },
        vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage,         totalStorageImg  },
    };

    vk::DescriptorPoolCreateInfo pci{};
    // Allow freeing sets individually if you recreate per-frame (optional but handy)
    pci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pci.maxSets = setsPerFrame * frames + extraSets;  // <-- IMPORTANT
    pci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pci.pPoolSizes = sizes.data();

    m_descPools.pool = m_device.createDescriptorPool(pci);
}

void VulkanRenderer::allocatePerFrameDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& fr = m_frames[i];

        std::array<vk::DescriptorSetLayout,2> layouts = { m_descLayouts.global, m_descLayouts.object };
        vk::DescriptorSetAllocateInfo ai{}; ai.descriptorPool = m_descPools.pool;
        ai.descriptorSetCount = layouts.size(); ai.pSetLayouts = layouts.data();
        auto out = m_device.allocateDescriptorSets(ai); //TODO: fix this line
        fr.globalSet = out[0];
        fr.objectSet = out[1];

        vk::DescriptorBufferInfo gbi{ fr.globalUBO.handle, 0, VK_WHOLE_SIZE };
        vk::DescriptorBufferInfo obi{ fr.objectUBO.handle, 0, VK_WHOLE_SIZE };

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = fr.globalSet; writes[0].dstBinding = 0; writes[0].descriptorType = vk::DescriptorType::eUniformBuffer; writes[0].descriptorCount = 1; writes[0].pBufferInfo = &gbi;
        writes[1].dstSet = fr.objectSet; writes[1].dstBinding = 0; writes[1].descriptorType = vk::DescriptorType::eUniformBuffer; writes[1].descriptorCount = 1; writes[1].pBufferInfo = &obi;

        m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void VulkanRenderer::createCommandPoolAndBuffers() {
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
    upci.flags = vk::CommandPoolCreateFlagBits::eTransient;
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

void VulkanRenderer::createSyncObjects() {
    for (auto& fr : m_frames) {
        vk::SemaphoreCreateInfo si{};
        fr.imageAvailable = m_device.createSemaphore(si);
        fr.renderFinished = m_device.createSemaphore(si);

        vk::FenceCreateInfo fi{};
        fi.flags = vk::FenceCreateFlagBits::eSignaled;
        fr.inFlight = m_device.createFence(fi);
    }
}

void VulkanRenderer::createPerFrameUniforms() {
    constexpr vk::DeviceSize defaultUBOSize = 64ull * 1024ull; // 64 KiB
    for (auto& fr : m_frames) {
        fr.globalUBO = createBuffer(defaultUBOSize, vk::BufferUsageFlagBits::eUniformBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
        fr.objectUBO = createBuffer(defaultUBOSize, vk::BufferUsageFlagBits::eUniformBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
    }
}

Buffer VulkanRenderer::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memUsage, bool mapped) {
    Buffer out{};
    out.size = size;
    out.usage = usage;

    vk::BufferCreateInfo bci{};
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo aci{};
    aci.usage = memUsage;
    aci.flags = allocFlags;

    VkBuffer buf = VK_NULL_HANDLE; VmaAllocation alloc = nullptr; VmaAllocationInfo ainfo{};
    if (vmaCreateBuffer(m_allocator, reinterpret_cast<const VkBufferCreateInfo*>(&bci), &aci, &buf, &alloc, &ainfo) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API failed to allocate buffer!");
    }

    out.handle = buf;
    out.alloc = alloc;
    out.alignment = ainfo.offset;
    //out.alignment = ainfo.requiredAlignment; // borken line. top is potential fix.
    if (mapped || (allocFlags & VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        out.mapped = ainfo.pMappedData;
        if (!out.mapped) vmaMapMemory(m_allocator, alloc, &out.mapped);
    }
    return out;
}

void VulkanRenderer::uploadToBuffer(const void *src, vk::DeviceSize size, Buffer &dst, vk::DeviceSize dstOffset) {
    if (dst.mapped) {
        std::memcpy(static_cast<char*>(dst.mapped) + dstOffset, src, static_cast<size_t>(size));
        vmaFlushAllocation(m_allocator, dst.alloc, dstOffset, size);
        return;
    }

    Buffer staging = createBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
    std::memcpy(staging.mapped, src, static_cast<size_t>(size));
    vmaFlushAllocation(m_allocator, staging.alloc, 0, size);

    copyBuffer(staging, dst, size);

    vmaDestroyBuffer(m_allocator, staging.handle, staging.alloc);
}

void VulkanRenderer::copyBuffer(Buffer &src, Buffer &dst, vk::DeviceSize size) {
    m_device.resetFences(m_uploadFence);
    m_device.resetCommandPool(m_uploadPool);

    vk::CommandBufferBeginInfo bi{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    m_uploadCmd.begin(bi);

    vk::BufferCopy cpy{};
    cpy.size = size;
    cpy.srcOffset = 0;
    cpy.dstOffset = 0;
    m_uploadCmd.copyBuffer(src.handle, dst.handle, 1, &cpy);

    m_uploadCmd.end();

    vk::CommandBufferSubmitInfo cbsi{};
    cbsi.commandBuffer = m_uploadCmd;
    vk::SubmitInfo2 si{};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbsi;
    auto r = m_graphicsQueue.submit2(1, &si, m_uploadFence);
    (void)m_device.waitForFences(m_uploadFence, VK_TRUE, UINT64_MAX);
}

Image VulkanRenderer::createImage(uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageTiling tiling, vk::SampleCountFlagBits samples, uint32_t mipLevels, uint32_t layers, VmaMemoryUsage memUsage) {
    Image out{};
    out.format = fmt;
    out.extent = vk::Extent3D{ w, h, 1 };
    out.mipLevels = mipLevels;
    out.layers = layers;
    out.sampled = samples;

    vk::ImageCreateInfo ici{};
    ici.imageType = vk::ImageType::e2D;
    ici.extent = out.extent;
    ici.mipLevels = mipLevels;
    ici.arrayLayers = layers;
    ici.format = fmt;
    ici.tiling = tiling;
    ici.initialLayout = vk::ImageLayout::eUndefined;
    ici.usage = usage;
    ici.samples = samples;
    ici.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo aci{};
    aci.usage = memUsage;

    VkImage img = VK_NULL_HANDLE; VmaAllocation alloc = nullptr; VmaAllocationInfo ainfo{};
    if (vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&ici), &aci, &img, &alloc, &ainfo) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API failed to create image!");
    }

    out.handle = img;
    out.alloc = alloc;
    out.currentLayout = vk::ImageLayout::eUndefined;

    // default view
    vk::ImageViewCreateInfo vci{};
    vci.image = out.handle;
    vci.viewType = vk::ImageViewType::e2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = aspectFromFormat(fmt);
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = mipLevels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = layers;
    out.view = m_device.createImageView(vci);

    return out;
}

void VulkanRenderer::transitionImage(Image &img, vk::ImageLayout newLayout, vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess, vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess, vk::ImageAspectFlags aspect, uint32_t baseMip, uint32_t levelCount, uint32_t baseLayer, uint32_t layerCount) {
    m_device.resetFences(m_uploadFence);
    m_device.resetCommandPool(m_uploadPool);

    vk::CommandBufferBeginInfo bi{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    m_uploadCmd.begin(bi);

    if (levelCount == VK_REMAINING_MIP_LEVELS) levelCount = img.mipLevels - baseMip;
    if (layerCount == VK_REMAINING_ARRAY_LAYERS) layerCount = img.layers - baseLayer;

    vk::ImageMemoryBarrier2 b{};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = img.currentLayout;
    b.newLayout = newLayout;
    b.image = img.handle;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.baseMipLevel = baseMip;
    b.subresourceRange.levelCount = levelCount;
    b.subresourceRange.baseArrayLayer = baseLayer;
    b.subresourceRange.layerCount = layerCount;

    vk::DependencyInfo dep{};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    m_uploadCmd.pipelineBarrier2(dep);

    m_uploadCmd.end();

    vk::CommandBufferSubmitInfo cbsi{};
    cbsi.commandBuffer = m_uploadCmd;

    vk::SubmitInfo2 si{};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbsi;
    auto r = m_graphicsQueue.submit2(1, &si, m_uploadFence);
    (void)m_device.waitForFences(m_uploadFence, VK_TRUE, UINT64_MAX);

    img.currentLayout = newLayout;
}

void VulkanRenderer::copyBufferToImage(Buffer &staging, Image &img, uint32_t w, uint32_t h, uint32_t baseLayer, uint32_t layerCount) {
    m_device.resetFences(m_uploadFence);
    m_device.resetCommandPool(m_uploadPool);

    vk::CommandBufferBeginInfo bi{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    m_uploadCmd.begin(bi);

    vk::BufferImageCopy copy{};
    copy.imageSubresource.aspectMask = aspectFromFormat(img.format);
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = baseLayer;
    copy.imageSubresource.layerCount = layerCount;
    copy.imageExtent = vk::Extent3D{ w, h, 1 };

    m_uploadCmd.copyBufferToImage(staging.handle, img.handle, vk::ImageLayout::eTransferDstOptimal, 1, &copy);

    m_uploadCmd.end();
    vk::CommandBufferSubmitInfo cbsi{};
    cbsi.commandBuffer = m_uploadCmd;

    vk::SubmitInfo2 si{};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbsi;
    auto r = m_graphicsQueue.submit2(1, &si, m_uploadFence);
    (void)m_device.waitForFences(m_uploadFence, VK_TRUE, UINT64_MAX);
}

void VulkanRenderer::generateMipmaps(Image &img) {
    // verify linear blit support
    auto props = m_physicalDevice.getFormatProperties(img.format);
    if (!(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        return; // no mips generated
    }

    m_device.resetFences(m_uploadFence);
    m_device.resetCommandPool(m_uploadPool);

    vk::CommandBufferBeginInfo bi{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    m_uploadCmd.begin(bi);

    vk::ImageMemoryBarrier2 barrier{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
    barrier.image = img.handle;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = img.layers;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipW = static_cast<int32_t>(img.extent.width);
    int32_t mipH = static_cast<int32_t>(img.extent.height);

    for (uint32_t i = 1; i < img.mipLevels; ++i) {
        // transition (i-1) to TRANSFER_SRC_OPTIMAL
        barrier.oldLayout = (i == 1) ? vk::ImageLayout::eTransferDstOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.subresourceRange.baseMipLevel = i - 1;
        {
            vk::DependencyInfo dep{};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrier;
            m_uploadCmd.pipelineBarrier2(dep);
        }

        // blit from (i-1) -> (i)
        vk::ImageBlit2 blit{};
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = img.layers;
        blit.srcOffsets[0] = vk::Offset3D{0,0,0};
        blit.srcOffsets[1] = vk::Offset3D{mipW,mipH,1};

        int32_t nextW = std::max(1, mipW / 2);
        int32_t nextH = std::max(1, mipH / 2);

        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = img.layers;
        blit.dstOffsets[0] = vk::Offset3D{ 0,0,0 };
        blit.dstOffsets[1] = vk::Offset3D{nextW,nextH,1};

        vk::BlitImageInfo2 bi2{};
        bi2.srcImage = img.handle;
        bi2.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        bi2.dstImage = img.handle;
        bi2.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        bi2.regionCount = 1;
        bi2.pRegions = { &blit };
        bi2.filter = vk::Filter::eLinear;
        m_uploadCmd.blitImage2(bi2);

        // Transition (i-1) to SHADER_READ_ONLY
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        {
            vk::DependencyInfo dep{};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrier;
            m_uploadCmd.pipelineBarrier2(dep);
        }

        mipW = nextW;
        mipH = nextH;
    }

    // transition last level to sahder read
    barrier.subresourceRange.baseMipLevel = img.mipLevels - 1;
    barrier.subresourceRange.levelCount = 1;
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    {
        vk::DependencyInfo dep{};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        m_uploadCmd.pipelineBarrier2(dep);
    }

    m_uploadCmd.end();

    vk::CommandBufferSubmitInfo cbsi{};
    cbsi.commandBuffer = m_uploadCmd;

    vk::SubmitInfo2 si{};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbsi;
    auto r = m_graphicsQueue.submit2(1, &si, m_uploadFence);
    (void)m_device.waitForFences(m_uploadFence, VK_TRUE, UINT64_MAX);

    img.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void VulkanRenderer::beginRendering(vk::CommandBuffer cmd, vk::ImageView colorView, vk::ImageView depthView, vk::Extent2D extent, const std::array<float, 4> &clearColor, float clearDepth, uint32_t clearStencil) {
    vk::ClearValue cv{};
    cv.color = vk::ClearColorValue{ std::array<float, 4>{clearColor[0], clearColor[1], clearColor[2], clearColor[3] } };

    vk::ClearValue dv{};
    dv.depthStencil = vk::ClearDepthStencilValue{ clearDepth, clearStencil };

    vk::RenderingAttachmentInfo color{};
    color.imageView = colorView;
    color.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    color.loadOp = vk::AttachmentLoadOp::eClear;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.clearValue = cv;

    vk::RenderingAttachmentInfo depth{};
    depth.imageView = depthView;
    depth.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depth.loadOp = vk::AttachmentLoadOp::eClear;
    depth.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth.clearValue = dv;

    vk::RenderingInfo info{};
    info.renderArea = vk::Rect2D{ {0,0}, extent };
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = &depth;

    cmd.beginRendering(info);

    // this is a dynamic viewport/scissor covering whole target
    vk::Viewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    vk::Rect2D sc{{0,0}, extent};
    cmd.setViewport(0, 1, &vp);
    cmd.setScissor(0, 1, &sc);
}

void VulkanRenderer::endRendering(vk::CommandBuffer cmd) {
    cmd.endRendering();
}

vk::Format VulkanRenderer::findDepthFormat() const {
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
    return vk::Format::eD32Sfloat; // default i guess idk
}

vk::SampleCountFlagBits VulkanRenderer::getMaxUsableSampleCount() const {
    auto props = m_physicalDevice.getProperties().limits;
    vk::SampleCountFlags counts = props.framebufferColorSampleCounts & props.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e8) return vk::SampleCountFlagBits::e8;
    if (counts & vk::SampleCountFlagBits::e4) return vk::SampleCountFlagBits::e4;
    if (counts & vk::SampleCountFlagBits::e2) return vk::SampleCountFlagBits::e2;
    return vk::SampleCountFlagBits::e1;
}

void VulkanRenderer::recordGraphicsCommands(uint32_t imageIndex, const Camera &cam, const Scene &scene) {
    auto& fr = m_frames[m_frameIndex];
    auto cmd = fr.cmd;

    // transition swap image to COLOR_ATTACHMENT_OPTIMAL
    vk::ImageLayout oldSwapLayout = vk::ImageLayout::eUndefined;
    if (imageIndex < g_swapImageLayouts.size()) oldSwapLayout = g_swapImageLayouts[imageIndex];

    vk::ImageMemoryBarrier2 toColor{};
    toColor.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    toColor.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    toColor.srcAccessMask = {};
    toColor.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    toColor.oldLayout = oldSwapLayout;
    toColor.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    toColor.image = m_swapImages[imageIndex];
    toColor.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    toColor.subresourceRange.baseMipLevel = 0;
    toColor.subresourceRange.levelCount = 1;
    toColor.subresourceRange.baseArrayLayer = 0;
    toColor.subresourceRange.layerCount = 1;

    vk::DependencyInfo depToColor{};
    depToColor.imageMemoryBarrierCount = 1;
    depToColor.pImageMemoryBarriers = &toColor;
    cmd.pipelineBarrier2(depToColor);
    if (imageIndex < g_swapImageLayouts.size()) g_swapImageLayouts[imageIndex] = vk::ImageLayout::eColorAttachmentOptimal;

    // depth image to DEPTH_ATTACHMENT_OPTIMAL
    vk::ImageMemoryBarrier2 toDepth{};
    toDepth.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    toDepth.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    toDepth.srcAccessMask = {};
    toDepth.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    toDepth.oldLayout = m_depth.currentLayout;
    toDepth.newLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    toDepth.image = m_depth.handle;
    toDepth.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    toDepth.subresourceRange.baseMipLevel = 0;
    toDepth.subresourceRange.levelCount = 1;
    toDepth.subresourceRange.baseArrayLayer = 0;
    toDepth.subresourceRange.layerCount = 1;

    vk::DependencyInfo depToDepth{};
    depToDepth.imageMemoryBarrierCount = 1;
    depToDepth.pImageMemoryBarriers = &toDepth;
    cmd.pipelineBarrier2(depToDepth);
    m_depth.currentLayout = vk::ImageLayout::eDepthAttachmentOptimal;

    // COMPUTE
    if (m_rayImage.currentLayout != vk::ImageLayout::eGeneral) {
        transitionImage(m_rayImage,
            vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead,  // src (if sampling previously)
            vk::PipelineStageFlagBits2::eComputeShader,  vk::AccessFlagBits2::eShaderWrite, // dst
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_compute.handle);

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_compute.layout,
                           /*firstSet*/ 0,
                           /*descriptorSetCount*/ 1,
                           &fr.computeSet,
                           /*dynamicOffsetCount*/ 0, /*pDynamicOffsets*/ nullptr);

    ComputePC pc;
    pc.width  = static_cast<int32_t>(m_swapExtent.width);
    pc.height = static_cast<int32_t>(m_swapExtent.height);
    pc.tFrame = timer;
    timer += 0.001f;

    cmd.pushConstants(m_compute.layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputePC), &pc);

    // Dispatch sized to image
    const uint32_t localX = 8, localY = 8; // keep in sync with your CS
    uint32_t gx = (m_swapExtent.width  + localX - 1) / localX;
    uint32_t gy = (m_swapExtent.height + localY - 1) / localY;
    cmd.dispatch(gx, gy, 1);

    // Barrier: storage image (write) -> sampled (read)
    vk::ImageMemoryBarrier2 toSample{};
    toSample.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
    toSample.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    toSample.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
    toSample.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    toSample.oldLayout = vk::ImageLayout::eGeneral;
    toSample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    toSample.image = m_rayImage.handle;
    toSample.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

    vk::DependencyInfo depToSample{ {}, 0, nullptr, 0, nullptr, 1, &toSample };
    cmd.pipelineBarrier2(depToSample);
    m_rayImage.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    // begin render and clear
    // TODO: move clear color elsewhere
    const std::array<float, 4> clear = { 1.0f, 0.0f, 0.0f, 1.0f };
    beginRendering(cmd, m_swapViews[imageIndex], m_depth.view, m_swapExtent, clear, 1.0f, 0);

    // TODO: bind pipeline and draw here
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_gfx.handle);

    std::array<vk::DescriptorSet,3> gfxSets = { fr.globalSet, fr.objectSet, fr.materialSet };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_gfx.layout, 0,
                           static_cast<uint32_t>(gfxSets.size()), gfxSets.data(), 0, nullptr);

    vk::DeviceSize offsets[] = { 0 };
    cmd.bindVertexBuffers(0, 1, &m_fsVBO.handle, offsets);
    cmd.bindIndexBuffer(m_fsIBO.handle, 0, vk::IndexType::eUint32);
    cmd.drawIndexed(m_fsIndexCount, 1, 0, 0, 0);

    endRendering(cmd);

    // transition swap image to PRESENT_SRC
    vk::ImageMemoryBarrier2 toPresent{};
    toPresent.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    toPresent.dstStageMask = vk::PipelineStageFlagBits2::eNone;
    toPresent.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    toPresent.dstAccessMask = {};
    toPresent.oldLayout = (imageIndex < g_swapImageLayouts.size()) ? g_swapImageLayouts[imageIndex] : vk::ImageLayout::eColorAttachmentOptimal;
    toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
    toPresent.image = m_swapImages[imageIndex];
    toPresent.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    toPresent.subresourceRange.baseMipLevel = 0;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.baseArrayLayer = 0;
    toPresent.subresourceRange.layerCount = 1;

    vk::DependencyInfo depToPresent{};
    depToPresent.imageMemoryBarrierCount = 1;
    depToPresent.pImageMemoryBarriers = &toPresent;
    cmd.pipelineBarrier2(depToPresent);
    if (imageIndex < g_swapImageLayouts.size()) g_swapImageLayouts[imageIndex] = vk::ImageLayout::ePresentSrcKHR;
}

void VulkanRenderer::cleanupSwapChain() {
    if (m_depth.view)
        m_device.destroyImageView(m_depth.view);

    if (m_depth.handle) {
        vmaDestroyImage(m_allocator, m_depth.handle, m_depth.alloc);
    }
    m_depth = {};

    for (auto v : m_swapViews) if (v) m_device.destroyImageView(v);
    m_swapViews.clear();

    if (m_swapchain) m_device.destroySwapchainKHR(m_swapchain);
    m_swapchain = vk::SwapchainKHR{};
    m_swapImages.clear();

    for (auto s : g_presentSignals) if (s) m_device.destroySemaphore(s);
    g_presentSignals.clear();
    g_imagesInFlight.clear();
    g_swapImageLayouts.clear();
}

void VulkanRenderer::recreateSwapChain() {
    GLFWwindow* win = m_window->getGLFWwindow();

    // wait for the window to be non-zerp (i.e not minimized)
    int w = 0, h = 0; do { glfwGetFramebufferSize(win, &w, &h); glfwWaitEventsTimeout(0.016); } while (w == 0 && h == 0);

    m_device.waitIdle();
    cleanupSwapChain();
    createSwapChain();
    createDepthResources();
}

std::vector<char const *> VulkanRenderer::getRequiredExtensions() {
    uint32_t count = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> out(glfwExts, glfwExts + count);
#ifndef NDEBUG
    out.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    return out;
}

std::vector<char const *> VulkanRenderer::getRequiredDeviceExtensions() {
    std::vector<const char*> out = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
    };
    return out;
}

// everything below here probably wont stay long. just to get to feature parity
void VulkanRenderer::allocateMaterialAndComputeDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& fr = m_frames[i];

        // Allocate [ material , compute ]
        std::array<vk::DescriptorSetLayout,2> layouts = { m_descLayouts.material, m_descLayouts.compute };
        vk::DescriptorSetAllocateInfo ai{ m_descPools.pool,
                                          static_cast<uint32_t>(layouts.size()),
                                          layouts.data() };
        auto sets = m_device.allocateDescriptorSets(ai);
        fr.materialSet = sets[0];
        fr.computeSet  = sets[1];

        // Material: sampled read of the compute image
        vk::DescriptorImageInfo sampled{};
        sampled.sampler     = m_defaultSampler.handle;
        sampled.imageView   = m_rayImage.view;
        sampled.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // Compute: storage write into the same image
        vk::DescriptorImageInfo storage{};
        storage.imageView   = m_rayImage.view;
        storage.imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet,2> writes{};
        // set=2 material, binding=0 (combined image sampler)
        writes[0] = { fr.materialSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &sampled };
        // set=3 compute,   binding=0 (storage image)
        writes[1] = { fr.computeSet,  0, 0, 1, vk::DescriptorType::eStorageImage,         &storage };

        m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void VulkanRenderer::createFullscreenQuadBuffers() {
    // positions (x,y) + uv (u,v)
    const float verts[] = {
        -1.f,-1.f, 0.f,0.f,
         1.f,-1.f, 1.f,0.f,
         1.f, 1.f, 1.f,1.f,
        -1.f, 1.f, 0.f,1.f
    };
    const uint32_t idx[] = { 0,1,2, 2,3,0 };
    m_fsIndexCount = 6;

    m_fsVBO = createBuffer(sizeof(verts),
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, false);

    m_fsIBO = createBuffer(sizeof(idx),
        vk::BufferUsageFlagBits::eIndexBuffer  | vk::BufferUsageFlagBits::eTransferDst,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, false);

    // Upload via staging
    uploadToBuffer(verts, sizeof(verts), m_fsVBO, 0);
    uploadToBuffer(idx,   sizeof(idx),   m_fsIBO, 0);
}

void VulkanRenderer::createRayTarget() {
    // Destroy previous on resize
    if (m_rayImage.view)  m_device.destroyImageView(m_rayImage.view);
    if (m_rayImage.handle) vmaDestroyImage(m_allocator, m_rayImage.handle, m_rayImage.alloc);
    m_rayImage = {};

    // RGBA8 storage+sampled so CS can write and FS can sample
    m_rayImage = createImage(
        m_swapExtent.width, m_swapExtent.height,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1, 1, 1, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Transition to GENERAL for first compute write
    transitionImage(m_rayImage,
        vk::ImageLayout::eGeneral,
        vk::PipelineStageFlagBits2::eTopOfPipe, {},
        vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
        vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
}


}
