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

static vk::DescriptorBufferInfo suballocUbo(FrameResources& fr, size_t bytes, size_t alignment = 256) {
    fr.uboHead = (fr.uboHead + alignment - 1) & ~(alignment - 1);
    if (fr.uboHead + bytes > fr.globalUBO.size) {
        bytes = std::min<size_t>(bytes, size_t(fr.globalUBO.size - fr.uboHead));
    }
    auto offset = fr.uboHead;
    fr.uboHead += bytes;
    return vk::DescriptorBufferInfo{ fr.globalUBO.handle, offset, bytes };
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

    DescriptorPoolSizes poolCfg{};
    poolCfg.sizes = {
        {vk::DescriptorType::eCombinedImageSampler, 512},
        {vk::DescriptorType::eSampledImage, 512},
        {vk::DescriptorType::eStorageImage, 512},
        {vk::DescriptorType::eUniformBuffer, 512},
        {vk::DescriptorType::eStorageBuffer, 512}
    };
    poolCfg.maxSets = 2048;
    m_descriptorSystem = std::make_unique<DescriptorSystem>(); m_descriptorSystem->init(m_device, poolCfg);

    m_pipelineSystem = std::make_unique<PipelineSystem>(); m_pipelineSystem->init(m_device, m_physicalDevice, m_shaderSystem.get(), m_descriptorSystem.get());

    createPerFrameUniforms();

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

void VulkanRenderer::drawFrame(const Camera &cam, const Scene &scene) {
    FrameResources& fr = m_frames[m_frameIndex];

    // acquire
    uint32_t imageIndex = 0;
    vk::Result acq = m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX, fr.imageAvailable, {}, &imageIndex);
    if (acq == vk::Result::eErrorOutOfDateKHR) { m_swapchainDirty = true; return; }
    if (acq == vk::Result::eSuboptimalKHR) { m_swapchainDirty = true; }

    // ensure swapchain image not in flight
    if (imageIndex < m_imagesInFlight.size() && m_imagesInFlight[imageIndex]) {
        (void)m_device.waitForFences(m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    if (imageIndex >= m_imagesInFlight.size()) {
        m_imagesInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
    }
    m_imagesInFlight[imageIndex] = fr.inFlight;

    // record
    vk::CommandBufferBeginInfo bi{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
    fr.cmd.begin(bi);
    //recordGraphicsCommands(imageIndex, cam, scene);
    fr.cmd.end();

    // submit
    vk::SemaphoreSubmitInfo waitSem{};
    waitSem.semaphore = fr.imageAvailable;
    waitSem.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

    vk::Semaphore presentSignal = m_presentSignals[imageIndex];

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
        if (fr.cmdPool) { m_device.destroyCommandPool(fr.cmdPool); }
        if (fr.imageAvailable) { m_device.destroySemaphore(fr.imageAvailable); }
        if (fr.renderFinished) { m_device.destroySemaphore(fr.renderFinished); }
        if (fr.inFlight) { m_device.destroyFence(fr.inFlight); }
    }

    if (m_uploadFence) { m_device.destroyFence(m_uploadFence); }
    if (m_uploadCmd) { m_device.freeCommandBuffers(m_uploadPool, 1, &m_uploadCmd); }
    if (m_uploadPool) { m_device.destroyCommandPool(m_uploadPool); }

    m_pipelineSystem->shutdown();
    m_descriptorSystem->shutdown();
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
        //if (!out.mapped) vmaMapMemory(m_allocator, alloc, &out.mapped);
    }
    return out;
}

void VulkanRenderer::uploadToBuffer(const void *src, vk::DeviceSize size, Buffer &dst, vk::DeviceSize dstOffset) {
    if (dst.mapped) {
        //std::memcpy(static_cast<char*>(dst.mapped) + dstOffset, src, static_cast<size_t>(size));
        vmaFlushAllocation(m_allocator, dst.alloc, dstOffset, size);
        return;
    }

    Buffer staging = createBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
    //std::memcpy(staging.mapped, src, static_cast<size_t>(size));
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

void VulkanRenderer::recordGraphicsCommands(uint32_t imageIndex, const Camera &cam, const VulkanScene &scene) {
    auto& fr = m_frames[m_frameIndex];
    fr.uboHead = 0; // reset per-frame UBO arena

    const auto clear = std::array<float,4>{0.05f, 0.08f, 0.10f, 1.0f };
    bool renderingActive = false;

    auto beginIfNeeded = [&]{
        if (renderingActive) return;
        ImageTransitions tr(fr.cmd);
        // Ensure layouts for color and depth
        Image swapImg{};
        swapImg.handle = m_swapImages[imageIndex];
        swapImg.view   = m_swapViews[imageIndex];
        swapImg.format = m_colorFormat;
        swapImg.layers = 1;
        swapImg.mipLevels = 1;
        swapImg.currentLayout = m_swapImageLayouts[imageIndex];
        tr.ensure(swapImg, Role::ColorAttachment);
        m_swapImageLayouts[imageIndex] = swapImg.currentLayout;
        tr.ensure(m_depth, Role::DepthAttachment);
        beginRendering(fr.cmd, m_swapViews[imageIndex], m_depth.view, m_swapExtent, clear);
        renderingActive = true;
    };
    auto endIfNeeded = [&]{
        if (!renderingActive) return;
        endRendering(fr.cmd);
        ImageTransitions tr(fr.cmd);
        Image swapImg{};
        swapImg.handle = m_swapImages[imageIndex];
        swapImg.view   = m_swapViews[imageIndex];
        swapImg.format = m_colorFormat;
        swapImg.layers = 1;
        swapImg.mipLevels = 1;
        swapImg.currentLayout = m_swapImageLayouts[imageIndex];
        tr.ensure(swapImg, Role::Present);
        m_swapImageLayouts[imageIndex] = swapImg.currentLayout;
        renderingActive = false;
    };

    for (const auto& pass : scene.passes)
    {
        const auto& prog = m_pipelineSystem->get(pass.pipelineName);
        const bool isComp = pass.isCompute;

        // Compute must NOT be inside a rendering instance
        if (isComp) endIfNeeded(); else beginIfNeeded();

        // Bind pipeline
        const auto bindPoint = isComp ? vk::PipelineBindPoint::eCompute
                                      : vk::PipelineBindPoint::eGraphics;
        fr.cmd.bindPipeline(bindPoint, prog.pipeline.get());

                // Push constants (if provided by pass & required by shader)
        /* TODO: push constants via PipelineSystem when exposed */

        if (isComp) {
            fr.cmd.dispatch(pass.groupsX, pass.groupsY, pass.groupsZ);
        } else {
            // dynamic viewport/scissor every graphics pass
            const vk::Viewport vp{ 0.f, 0.f,
                                   float(m_swapExtent.width), float(m_swapExtent.height),
                                   0.f, 1.f };
            const vk::Rect2D   sc{ {0,0}, m_swapExtent };
            fr.cmd.setViewport(0, 1, &vp);
            fr.cmd.setScissor (0, 1, &sc);

            fr.cmd.draw(pass.vertexCount, 1, 0, 0);
        }
    }

    endIfNeeded(); // close rendering if last pass was graphics
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