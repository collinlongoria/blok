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
#include <set>

#define GLFW_INCLUDE_NONE
#include "image_states.hpp"
#include "GLFW/glfw3.h"

#include "shader_pipe.hpp"
#include "window.hpp"

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

    m_shaderSystem = std::make_unique<ShaderSystem>(); m_shaderSystem->init(m_device);

    m_pipelineSystem = std::make_unique<PipelineSystem>(); m_pipelineSystem->init(m_device, m_physicalDevice, m_shaderSystem.get());

    createPerFrameUniforms();

    std::cout << "Vulkan API initialized!"  << std::endl;

    // Ensure hello_triangle is parsed and created from YAML
    const std::string helloYaml = "assets/pipelines/hello_triangle.yaml"; // adjust path if needed
    auto created = m_pipelineSystem->loadPipelinesFromYAML(helloYaml);
    if (std::find(created.begin(), created.end(), std::string("hello_triangle")) == created.end()) {
        throw std::runtime_error("failed to create 'hello_triangle' from " + helloYaml);
    }
}

void VulkanRenderer::beginFrame() {
    // Reset UBO allocator head for this frame
    m_frames[m_frameIndex].uboHead = 0;
}

void VulkanRenderer::drawFrame(const Camera &cam, const Scene &scene) {
    auto& fr = m_frames[m_frameIndex];

    // wait and reset
    if (m_device.waitForFences(1, &fr.inFlight, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
        throw std::runtime_error("waitForFences failed");
    auto result = m_device.resetFences(1, &fr.inFlight);

    // acquire
    uint32_t imageIndex = 0;
    const auto acq = m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX,
                                                  fr.imageAvailable, nullptr, &imageIndex);
    if (acq == vk::Result::eErrorOutOfDateKHR) { m_swapchainDirty = true; endFrame(); return; }
    if (acq != vk::Result::eSuccess && acq != vk::Result::eSuboptimalKHR)
        throw std::runtime_error("acquireNextImageKHR failed");

    if (m_imagesInFlight[imageIndex]) {
        result = m_device.waitForFences(1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    m_imagesInFlight[imageIndex] = fr.inFlight;

    // record
    fr.cmd.reset({});
    vk::CommandBufferBeginInfo bi{};
    fr.cmd.begin(bi);

    blok::ImageTransitions it{ fr.cmd };

    Image sw{};
    sw.handle        = m_swapImages[imageIndex];
    sw.view          = m_swapViews[imageIndex];
    sw.width         = m_swapExtent.width;
    sw.height        = m_swapExtent.height;
    sw.mipLevels     = 1;
    sw.layers        = 1;
    sw.format        = m_colorFormat;
    sw.currentLayout = m_swapImageLayouts[imageIndex];

    it.ensure(sw, blok::Role::ColorAttachment);
    it.ensure(m_depth, blok::Role::DepthAttachment);

    const std::array<float,4> clear{1.0f,0.0f,0.0f,1.0f};
    beginRendering(fr.cmd, sw.view, m_depth.view, m_swapExtent, clear, 1.0f, 0);

    // viewport/scissor
    vk::Viewport vp{0.f, 0.f, float(m_swapExtent.width), float(m_swapExtent.height), 0.f, 1.f};
    vk::Rect2D sc{{0,0}, m_swapExtent};
    fr.cmd.setViewport(0, 1, &vp);
    fr.cmd.setScissor(0, 1, &sc);

    if (m_renderList) {
        vk::Pipeline lastPipe{};
        for (const auto& obj : *m_renderList) {
            // bind pipeline by material name
            const auto& prog = m_pipelineSystem->get(obj.material.pipelineName);
            if (prog.pipeline.get() != lastPipe) {
                fr.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, prog.pipeline.get());
                lastPipe = prog.pipeline.get();
            }

            // bind vertex/index and draw
            vk::DeviceSize off = 0;
            if (obj.mesh.vertex) {
                fr.cmd.bindVertexBuffers(0, 1, &obj.mesh.vertex, &off);
                if (obj.mesh.index && obj.mesh.indexCount) {
                    fr.cmd.bindIndexBuffer(obj.mesh.index, 0, vk::IndexType::eUint32);
                    fr.cmd.drawIndexed(obj.mesh.indexCount, 1, 0, 0, 0);
                } else {
                    fr.cmd.draw(obj.mesh.vertexCount, 1, 0, 0);
                }
            }
        }
    }

    endRendering(fr.cmd);

    it.ensure(sw, blok::Role::Present);
    m_swapImageLayouts[imageIndex] = sw.currentLayout;

    fr.cmd.end();

    // submit + present with per-image semaphores
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo si{};
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &fr.imageAvailable;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &fr.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_presentSignals[imageIndex];

    result = m_graphicsQueue.submit(1, &si, fr.inFlight);

    vk::PresentInfoKHR pi{};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_presentSignals[imageIndex];
    vk::SwapchainKHR scH  = m_swapchain;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &scH;
    pi.pImageIndices      = &imageIndex;

    const auto pres = m_presentQueue.presentKHR(pi);
    if (pres == vk::Result::eErrorOutOfDateKHR || pres == vk::Result::eSuboptimalKHR) m_swapchainDirty = true;
    else if (pres != vk::Result::eSuccess) throw std::runtime_error("presentKHR failed");
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
        if (fr.cmdPool) { m_device.destroyCommandPool(fr.cmdPool); }
        if (fr.imageAvailable) { m_device.destroySemaphore(fr.imageAvailable); }
        if (fr.renderFinished) { m_device.destroySemaphore(fr.renderFinished); }
        if (fr.inFlight) { m_device.destroyFence(fr.inFlight); }
    }

    // owned buffers
    for (auto& b : m_ownedBuffers) {
        if (b.handle) vmaDestroyBuffer(m_allocator, b.handle, b.alloc);
    }
    m_ownedBuffers.clear();

    if (m_uploadFence) { m_device.destroyFence(m_uploadFence); }
    if (m_uploadCmd) { m_device.freeCommandBuffers(m_uploadPool, 1, &m_uploadCmd); }
    if (m_uploadPool) { m_device.destroyCommandPool(m_uploadPool); }

    m_pipelineSystem->shutdown();

    m_shaderSystem->shutdown();

    cleanupSwapChain();

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
    for (auto s : m_presentSignals) if (s) m_device.destroySemaphore(s);
    m_presentSignals.clear();
    m_presentSignals.resize(m_swapImages.size());
    for (auto &s : m_presentSignals) { s = m_device.createSemaphore({}); }

    m_imagesInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
    m_swapImageLayouts.assign(m_swapImages.size(), vk::ImageLayout::eUndefined);
}

void VulkanRenderer::createDepthResources() {
    m_depthFormat = findDepthFormat();

    // getMaxUsableSampleCount()
    constexpr vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

    m_depth = createImage(m_swapExtent.width, m_swapExtent.height, m_depthFormat,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        vk::ImageTiling::eOptimal, samples, 1, 1, VMA_MEMORY_USAGE_AUTO);
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
    }
}

Buffer VulkanRenderer::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memUsage, bool mapped) {
    Buffer out{};
    out.size = size;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = static_cast<VkDeviceSize>(size);
    bci.usage = static_cast<VkBufferUsageFlags>(usage);
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.flags = allocFlags | (mapped ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0);
    aci.usage = memUsage;

    VmaAllocationInfo ainfo{};
    if (vmaCreateBuffer(m_allocator, &bci, &aci, reinterpret_cast<VkBuffer*>(&out.handle),
                        &out.alloc, &ainfo) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API: vmaCreateBuffer failed");
                        }
    if (mapped) out.mapped = ainfo.pMappedData;
    return out;
}

void VulkanRenderer::uploadToBuffer(const void *src, vk::DeviceSize size, Buffer &dst, vk::DeviceSize dstOffset) {
    if (dst.mapped) {
        std::memcpy(static_cast<char*>(dst.mapped) + dstOffset, src, static_cast<size_t>(size));
        return;
    }
    // Staging path
    Buffer staging = createBuffer(size,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                  VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
    std::memcpy(staging.mapped, src, static_cast<size_t>(size));
    copyBuffer(staging, dst, size);
    vmaDestroyBuffer(m_allocator, staging.handle, staging.alloc);
}

void VulkanRenderer::copyBuffer(Buffer &src, Buffer &dst, vk::DeviceSize size) {
    // one-shot on m_uploadCmd
    auto result = m_device.resetFences(1, &m_uploadFence);
    m_uploadCmd.reset({});

    vk::CommandBufferBeginInfo bi{};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    m_uploadCmd.begin(bi);

    vk::BufferCopy copy{0, 0, size};
    m_uploadCmd.copyBuffer(src.handle, dst.handle, 1, &copy);

    m_uploadCmd.end();

    vk::SubmitInfo si{};
    si.commandBufferCount = 1; si.pCommandBuffers = &m_uploadCmd;
    result = m_graphicsQueue.submit(1, &si, m_uploadFence);
    result = m_device.waitForFences(1, &m_uploadFence, VK_TRUE, UINT64_MAX);
}

Image VulkanRenderer::createImage(uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageTiling tiling, vk::SampleCountFlagBits samples, uint32_t mipLevels, uint32_t layers, VmaMemoryUsage memUsage) {
    Image out{};
    out.width = w; out.height = h; out.format = fmt; out.mipLevels = mipLevels; out.layers = layers; out.samples = samples;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent = { w, h, 1 };
    ici.mipLevels = mipLevels;
    ici.arrayLayers = layers;
    ici.format = static_cast<VkFormat>(fmt);
    ici.tiling = static_cast<VkImageTiling>(tiling);
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = static_cast<VkImageUsageFlags>(usage);
    ici.samples = static_cast<VkSampleCountFlagBits>(samples);
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = memUsage;

    if (vmaCreateImage(m_allocator, &ici, &aci,
                       reinterpret_cast<VkImage*>(&out.handle), &out.alloc, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API: vmaCreateImage failed");
                       }

    vk::ImageViewCreateInfo vci{};
    vci.image = out.handle;
    vci.viewType = vk::ImageViewType::e2D;
    vci.format = fmt;
    vci.subresourceRange = { aspectFromFormat(fmt), 0, mipLevels, 0, layers };
    out.view = m_device.createImageView(vci);

    out.currentLayout = vk::ImageLayout::eUndefined;
    return out;
}

void VulkanRenderer::copyBufferToImage(Buffer &staging, Image &img, uint32_t w, uint32_t h, uint32_t baseLayer, uint32_t layerCount) {
    auto& fr = m_frames[m_frameIndex];

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = aspectFromFormat(img.format);
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = baseLayer;
    region.imageSubresource.layerCount = layerCount;
    region.imageExtent = vk::Extent3D{ w, h, 1 };

    fr.cmd.copyBufferToImage(staging.handle, img.handle, vk::ImageLayout::eTransferDstOptimal, 1, &region);
}

vk::Buffer VulkanRenderer::uploadVertexBuffer(const void *data, vk::DeviceSize sizeBytes, uint32_t vertexCount) {
    Buffer dst = createBuffer(sizeBytes,
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, false);
    uploadToBuffer(data, sizeBytes, dst, 0);
    m_ownedBuffers.push_back(dst);
    return dst.handle;
}

vk::Buffer VulkanRenderer::uploadIndexBuffer(const uint32_t *data, uint32_t indexCount) {
    const vk::DeviceSize sizeBytes = sizeof(uint32_t) * indexCount;
    Buffer dst = createBuffer(sizeBytes,
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, false);
    uploadToBuffer(data, sizeBytes, dst, 0);
    m_ownedBuffers.push_back(dst);
    return dst.handle;
}

MeshBuffers VulkanRenderer::uploadMesh(const void *vertices, vk::DeviceSize bytes, uint32_t vertexCount, const uint32_t *indices, uint32_t indexCount) {
    MeshBuffers out{};
    out.vertex = uploadVertexBuffer(vertices, bytes, vertexCount);
    out.vertexCount = vertexCount;
    if (indices && indexCount) {
        out.index = uploadIndexBuffer(indices, indexCount);
        out.indexCount = indexCount;
    }
    return out;
}

void VulkanRenderer::setRenderList(const std::vector<Object> *list) {
    m_renderList = list;
}

void VulkanRenderer::generateMipmaps(Image &img) {
    if (img.mipLevels <= 1) return;
    auto& fr = m_frames[m_frameIndex];

    for (uint32_t i = 1; i < img.mipLevels; ++i) {
        // transition src mip i-1 to transfer src
        {
            vk::ImageMemoryBarrier2 b{};
            b.oldLayout = (i==1) ? vk::ImageLayout::eTransferDstOptimal : vk::ImageLayout::eTransferSrcOptimal;
            b.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            b.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            b.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            b.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            b.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            b.image = img.handle;
            b.subresourceRange = { aspectFromFormat(img.format), i-1, 1, 0, img.layers };
            vk::DependencyInfo dep{}; dep.setImageMemoryBarriers(b);
            fr.cmd.pipelineBarrier2(dep);
        }
        // transition dst mip i to transfer dst
        {
            vk::ImageMemoryBarrier2 b{};
            b.oldLayout = vk::ImageLayout::eUndefined;
            b.newLayout = vk::ImageLayout::eTransferDstOptimal;
            b.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            b.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            b.srcAccessMask = {};
            b.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            b.image = img.handle;
            b.subresourceRange = { aspectFromFormat(img.format), i, 1, 0, img.layers };
            vk::DependencyInfo dep{}; dep.setImageMemoryBarriers(b);
            fr.cmd.pipelineBarrier2(dep);
        }

        // blit
        vk::ImageBlit2 blit{};
        blit.srcSubresource = { aspectFromFormat(img.format), i-1, 0, img.layers };
        blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
        blit.srcOffsets[1] = vk::Offset3D{ static_cast<int32_t>(std::max(1u, img.width  >> (i-1))),
                                static_cast<int32_t>(std::max(1u, img.height >> (i-1))), 1 };
        blit.dstSubresource = { aspectFromFormat(img.format), i, 0, img.layers };
        blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
        blit.dstOffsets[1] = vk::Offset3D{ static_cast<int32_t>(std::max(1u, img.width  >> i)),
                                static_cast<int32_t>(std::max(1u, img.height >> i)), 1 };

        vk::BlitImageInfo2 bi{};
        bi.srcImage = img.handle; bi.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        bi.dstImage = img.handle; bi.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        bi.regionCount = 1; bi.pRegions = &blit;
        bi.filter = vk::Filter::eLinear;
        fr.cmd.blitImage2(bi);
    }
    // final transition whole image to shader read
    //transitionImage(img, vk::ImageLayout::eShaderReadOnlyOptimal,
                    //{}, {}, {}, {}, aspectFromFormat(img.format), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS);
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
    // TODO
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

    for (auto s : m_presentSignals) if (s) m_device.destroySemaphore(s);
    m_presentSignals.clear();
    m_imagesInFlight.clear();
    m_swapImageLayouts.clear();
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