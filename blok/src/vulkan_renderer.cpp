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

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include <filesystem>
#include <fstream>
#include <stb_image.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "shader_pipe.hpp"
#include "window.hpp"
#include "assimp/version.h"

namespace blok {

void recurseModelNodes(ModelData* meshdata,
                       const  aiScene* aiscene,
                       const  aiNode* node,
                       const aiMatrix4x4& parentTr,
                       const int level=0);

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
    const std::string helloYaml = "assets/pipelines/hello_triangle.yaml";
    auto created = m_pipelineSystem->loadPipelinesFromYAML(helloYaml);
    if (std::find(created.begin(), created.end(), std::string("hello_triangle")) == created.end()) {
        throw std::runtime_error("failed to create 'hello_triangle' from " + helloYaml);
    }

    const std::string meshYaml = "assets/pipelines/mesh_flat.yaml";
    created = m_pipelineSystem->loadPipelinesFromYAML(meshYaml);
    if (std::find(created.begin(), created.end(), std::string("mesh_flat")) == created.end()) {
        throw std::runtime_error("failed to create 'mesh_flat' from " + helloYaml);
    }

    const std::string rainbowYAML = "assets/pipelines/rainbow.yaml";
    created = m_pipelineSystem->loadPipelinesFromYAML(rainbowYAML);
    if (std::find(created.begin(), created.end(), std::string("rainbow")) == created.end()) {
        throw std::runtime_error("failed to create 'rainbow' from " + rainbowYAML);
    }

    DescriptorAllocatorGrowable::PoolSizeRatio ratios[] = {
        { vk::DescriptorType::eUniformBuffer, 4.0f},
        {vk::DescriptorType::eUniformBufferDynamic, 1.0f},
        {vk::DescriptorType::eCombinedImageSampler, 4.0f},
        {vk::DescriptorType::eStorageBuffer, 2.0f}
    };
    m_descAlloc.init(m_device, 512, std::span{ratios, std::size(ratios)});

    const auto& prog = m_pipelineSystem->get("mesh_flat");
    if (!prog.setLayouts.empty()) {
        for (auto& fr : m_frames) {
            fr.frameSet = m_descAlloc.allocate(m_device, prog.setLayouts[0]);

            DescriptorWriter w;
            w.write_buffer(0, fr.globalUBO.handle, sizeof(FrameUBO), 0, vk::DescriptorType::eUniformBuffer);
            w.updateSet(m_device, fr.frameSet);
            w.clear();
        }
    }

    const auto& cprog = m_pipelineSystem->get("rainbow");
    if (!cprog.setLayouts.empty()) {
        for (auto& fr : m_frames) {
            fr.computeFrameSet = m_descAlloc.allocate(m_device, cprog.setLayouts[0]);

            DescriptorWriter w;
            w.write_buffer(0, fr.globalUBO.handle, sizeof(FrameUBO), 0,
                           vk::DescriptorType::eUniformBuffer);
            w.updateSet(m_device, fr.computeFrameSet);
            w.clear();
        }
    }

    vk::SamplerCreateInfo sci{};
    sci.magFilter = vk::Filter::eLinear;
    sci.minFilter = vk::Filter::eLinear;
    sci.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sci.addressModeU = sci.addressModeV = sci.addressModeW =
        vk::SamplerAddressMode::eRepeat;
    sci.maxLod = VK_LOD_CLAMP_NONE;

    Sampler s{};
    s.handle = m_device.createSampler(sci);
    m_samplers["default"] = s;

    initGUI();
}

void VulkanRenderer::initGUI() {
    // core stuff
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // hook window
    ImGui_ImplGlfw_InitForVulkan(m_window->getGLFWwindow(), true);

    vk::DescriptorPoolSize pool_sizes[] = {
        { vk::DescriptorType::eSampler,                1000 },
        { vk::DescriptorType::eCombinedImageSampler,   1000 },
        { vk::DescriptorType::eSampledImage,           1000 },
        { vk::DescriptorType::eStorageImage,           1000 },
        { vk::DescriptorType::eUniformTexelBuffer,     1000 },
        { vk::DescriptorType::eStorageTexelBuffer,     1000 },
        { vk::DescriptorType::eUniformBuffer,          1000 },
        { vk::DescriptorType::eStorageBuffer,          1000 },
        { vk::DescriptorType::eUniformBufferDynamic,   1000 },
        { vk::DescriptorType::eStorageBufferDynamic,   1000 },
        { vk::DescriptorType::eInputAttachment,        1000 }
    };

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000 * static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    m_imguiDescriptorPool = m_device.createDescriptorPool(pool_info);

    // init imgui
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance       = static_cast<VkInstance>(m_instance);
    init_info.PhysicalDevice = static_cast<VkPhysicalDevice>(m_physicalDevice);
    init_info.Device         = static_cast<VkDevice>(m_device);
    init_info.QueueFamily    = *m_qfi.graphics;
    init_info.Queue          = static_cast<VkQueue>(m_graphicsQueue);
    init_info.DescriptorPool = static_cast<VkDescriptorPool>(m_imguiDescriptorPool);
    init_info.MinImageCount  = static_cast<uint32_t>(m_swapImages.size());
    init_info.ImageCount     = static_cast<uint32_t>(m_swapImages.size());
    init_info.PipelineInfoMain.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator      = nullptr;
    init_info.CheckVkResultFn = nullptr;
    init_info.UseDynamicRendering = VK_TRUE;

    static VkFormat colorFormat;
    colorFormat = static_cast<VkFormat>(m_colorFormat);

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    init_info.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS) {
            throw std::runtime_error("ImGui Vulkan backend error");
        }
    };

    ImGui_ImplVulkan_Init(&init_info);

    // note: no longer need to upload the font to cmd buffer as of 2023
}

void VulkanRenderer::beginFrame() {
    // Reset UBO allocator head for this frame
    m_frames[m_frameIndex].uboHead = 0;

    // imgui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderer::drawFrame(const Camera &cam, const Scene &scene) {
    auto& fr = m_frames[m_frameIndex];

    const float aspect = static_cast<float>(m_swapExtent.width) / static_cast<float>(m_swapExtent.height);
    // write frame UBO (offset 0)
    const vk::DeviceSize minAlign = m_physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    static float t = 0.0f;
    t += 0.00167f;
    FrameUBO fubo{ cam.view(), cam.projection(aspect, nearPlane, farPlane), t, {} };
    uploadToBuffer(&fubo, sizeof(FrameUBO), fr.globalUBO, 0);
    fr.uboHead = alignUp(sizeof(FrameUBO), minAlign);


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

       // Compute
    if (false) {
        const auto& cprog = m_pipelineSystem->get("rainbow");

        fr.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, cprog.pipeline.get());

        fr.cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cprog.layout.get(),
                                  /*firstSet*/0, /*count*/1, &fr.computeFrameSet, 0, nullptr);


        if (m_renderList) {
            for (const auto& obj : *m_renderList) {
                if (!obj.mesh.vertex || obj.mesh.vertexCount == 0) continue;

                // Allocate per-object set 1 for the vertex storage buffer
                vk::DescriptorSet objSet = m_descAlloc.allocate(m_device, cprog.setLayouts[1]);
                DescriptorWriter w;
                const vk::DeviceSize bytes = obj.mesh.vertexCount * 24; // 6 floats * 4 bytes
                w.write_buffer(/*binding*/0, static_cast<vk::Buffer>(obj.mesh.vertex),
                               bytes, /*offset*/0, vk::DescriptorType::eStorageBuffer);
                w.updateSet(m_device, objSet);
                w.clear();

                // Bind set 1
                fr.cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cprog.layout.get(),
                                          /*firstSet*/1, /*count*/1, &objSet, 0, nullptr);

                // Push vertexCount
                uint32_t vcount = obj.mesh.vertexCount;
                fr.cmd.pushConstants<uint32_t>(cprog.layout.get(), vk::ShaderStageFlagBits::eCompute, 0, vcount);

                // Dispatch: groups of 64
                uint32_t groups = (vcount + 63) / 64;
                fr.cmd.dispatch(groups, 1, 1);

                // Barrier: make compute writes visible to vertex input (graphics)
                vk::BufferMemoryBarrier2 b{};
                b.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
                b.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
                b.dstStageMask  = vk::PipelineStageFlagBits2::eVertexInput;
                b.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
                b.buffer        = static_cast<vk::Buffer>(obj.mesh.vertex);
                b.offset        = 0;
                b.size          = VK_WHOLE_SIZE;

                vk::DependencyInfo dep{}; dep.setBufferMemoryBarriers(b);
                fr.cmd.pipelineBarrier2(dep);
            }
        }
    }
    // End Compute

    const std::array<float,4> clear{0.0f,0.0f,0.0f,1.0f};
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
            const auto& prog = m_pipelineSystem->get(obj.pipelineName);
            if (prog.pipeline.get() != lastPipe) {
                fr.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, prog.pipeline.get());
                lastPipe = prog.pipeline.get();

                // bind set 0 once per pipeline switch
                if (!prog.setLayouts.empty()) {
                    fr.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, prog.layout.get(), 0, 1, &fr.frameSet, 0, nullptr);
                }
            }

            // write object UBO into frame buffer
            ObjectUBO oubo{ obj.model.getTransformMatrix() };
            const vk::DeviceSize minAlign = m_physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
            vk::DeviceSize objOffset = fr.uboHead;
            uploadToBuffer(&oubo, sizeof(ObjectUBO), fr.globalUBO, objOffset);
            fr.uboHead = alignUp(fr.uboHead + sizeof(ObjectUBO), minAlign);

            // allocate and update set 1 for this object
            // TODO: consider material set 1 and per object set 2
            vk::DescriptorSet objSet{};
            if (prog.setLayouts.size() >= 2) {
                objSet = m_descAlloc.allocate(m_device, prog.setLayouts[1]);
                DescriptorWriter w;
                w.write_buffer(0, fr.globalUBO.handle, sizeof(ObjectUBO), objOffset, vk::DescriptorType::eUniformBuffer);
                w.updateSet(m_device, objSet);
                w.clear();

                // bind sets
                const vk::DescriptorSet sets[] = { fr.frameSet, objSet };
                fr.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, prog.layout.get(), 1, 1, &objSet, 0, nullptr);
            }

            // bind material set (set = 2) if present
            if (prog.setLayouts.size() >= 3 && obj.material.materialSet) {
                fr.cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    prog.layout.get(),
                    2,
                    1,
                    &obj.material.materialSet,
                    0,
                    nullptr
                );
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

    // imgui
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fr.cmd);

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

    // imgui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiDescriptorPool) {
        m_device.destroyDescriptorPool(m_imguiDescriptorPool);
        m_imguiDescriptorPool = nullptr;
    }

    // per frame resources
    for (auto& fr : m_frames) {
        if (fr.globalUBO.handle) { vmaDestroyBuffer(m_allocator, fr.globalUBO.handle, fr.globalUBO.alloc); fr.globalUBO.handle = nullptr; fr.globalUBO.alloc = nullptr; }
        if (fr.cmdPool) { m_device.destroyCommandPool(fr.cmdPool); }
        if (fr.imageAvailable) { m_device.destroySemaphore(fr.imageAvailable); }
        if (fr.renderFinished) { m_device.destroySemaphore(fr.renderFinished); }
        if (fr.inFlight) { m_device.destroyFence(fr.inFlight); }
    }

    // owned images
    for (auto& [_,i] : m_images) {
        if (i.view) {
            m_device.destroyImageView(i.view);
            i.view = nullptr;
        }
        if (i.handle) {
            vmaDestroyImage(m_allocator, i.handle, i.alloc);
            i.handle = nullptr;
            i.alloc = nullptr;
        }
    }
    m_images.clear();

    // named buffers
    for (auto& [_, b] : m_buffers) {
        if (b.handle) {
            vmaDestroyBuffer(m_allocator, b.handle, b.alloc);
            b.handle = nullptr;
            b.alloc = nullptr;
        }
    }
    m_buffers.clear();

    // meshes (not needed)
    m_meshes.clear();

    // owned buffers
    for (auto& b : m_ownedBuffers) {
        if (b.handle) vmaDestroyBuffer(m_allocator, b.handle, b.alloc);
    }
    m_ownedBuffers.clear();

    // samplers
    for (auto& [_, s] : m_samplers) {
        if (s.handle) {
            m_device.destroySampler(s.handle);
            s.handle = nullptr;
        }
    }
    m_samplers.clear();

    if (m_uploadFence) { m_device.destroyFence(m_uploadFence); }
    if (m_uploadCmd) { m_device.freeCommandBuffers(m_uploadPool, 1, &m_uploadCmd); }
    if (m_uploadPool) { m_device.destroyCommandPool(m_uploadPool); }

    m_descAlloc.destroyPools(m_device);

    m_pipelineSystem->shutdown();

    m_shaderSystem->shutdown();

    cleanupSwapChain();

    destroyAllocator();

    if (m_device) m_device.destroy();
    if (m_surface) m_instance.destroySurfaceKHR(m_surface);
    if (m_instance) m_instance.destroy();
}

void VulkanRenderer::buildMaterialSetForObject(Object& obj,
                                               const std::string& texturePath)
{
    const auto& prog = m_pipelineSystem->get(obj.pipelineName);

    // This pipeline must define set 2 in its YAML layout
    if (prog.setLayouts.size() < 3) {
        throw std::runtime_error("Pipeline '" + obj.pipelineName +
                                 "' has no set 2 layout for materials");
    }

    // Load or reuse texture
    Image img;
    if (auto it = m_images.find(texturePath); it != m_images.end()) {
        img = it->second;
    } else {
        img = loadTexture2D(texturePath, true);
    }

    // Get sampler (created once, reused)
    vk::Sampler sampler = m_samplers.at("default").handle;

    // Allocate set 2 using the layout that came from YAML
    vk::DescriptorSet matSet = m_descAlloc.allocate(m_device, prog.setLayouts[2]);

    // For this pipeline, set 2 / binding 0 is a combined_image_sampler
    DescriptorWriter w;
    w.write_image(0, img.view, sampler, vk::ImageLayout::eShaderReadOnlyOptimal, vk::DescriptorType::eCombinedImageSampler);
    w.updateSet(m_device, matSet);
    w.clear();

    obj.material.materialSet = matSet;
    // TODO: Optionally track that this material uses texture index X
    // obj.material.textureId = something;
}


Image VulkanRenderer::loadTexture2D(const std::string &path, bool generateMips) {
    int w, h, ch;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("failed to load texture " + path);
    }

    vk::DeviceSize size = static_cast<vk::DeviceSize>(w) * h * 4;

    // staging buffer
    Buffer staging = createBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
    std::memcpy(staging.mapped, pixels, static_cast<size_t>(size));
    stbi_image_free(pixels);

    // mips
    uint32_t mipLevels = 1;
    if (generateMips) {
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
    }

    Image img = createImage(
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h),
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        mipLevels,
        1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // record upload on dedicated transient cmd buffer
    auto r = m_device.resetFences(1, &m_uploadFence);
    m_uploadCmd.reset({});

    vk::CommandBufferBeginInfo bi{};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    m_uploadCmd.begin(bi);

    // layout: undefined -> transfer dst
    ImageTransitions it{ m_uploadCmd };
    it.ensure(img, Role::TransferDst);

    // copy level 0
    copyBufferToImage(m_uploadCmd, staging, img, w, h);

    // generate mipmaps and transition to shader read
    if (generateMips && mipLevels > 1) {
        generateMipmaps(m_uploadCmd, img);
    } else {
        // no mips: just transition full image to shader read
        vk::ImageMemoryBarrier2 b{};
        b.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
        b.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
        b.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        b.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
        b.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        b.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        b.image         = img.handle;
        b.subresourceRange = { aspectFromFormat(img.format), 0, img.mipLevels, 0, img.layers };

        vk::DependencyInfo dep{}; dep.setImageMemoryBarriers(b);
        m_uploadCmd.pipelineBarrier2(dep);
        img.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    m_uploadCmd.end();

    vk::SubmitInfo si{};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_uploadCmd;
    auto result = m_graphicsQueue.submit(1, &si, m_uploadFence);
    result = m_device.waitForFences(1, &m_uploadFence, VK_TRUE, UINT64_MAX);

    // keep staging buffer around or destroy here
    vmaDestroyBuffer(m_allocator, staging.handle, staging.alloc);

    m_images[path] = img;
    return img;
}

MeshBuffers VulkanRenderer::loadMeshOBJ(const std::string &path) {
    // Cache lookup: reuse GPU buffers if we’ve already loaded this OBJ
    auto it = m_meshes.find(path);
    if (it != m_meshes.end()) {
        return it->second;
    }

    // CPU-side mesh using Assimp
    ModelData model;
    if (!model.readAssimpFile(path, Matrix4(1.0f))) {
        throw std::runtime_error("Failed to load OBJ: " + path);
    }

    if (model.vertices.empty()) {
        throw std::runtime_error("OBJ has no vertices: " + path);
    }

    const vk::DeviceSize vbytes =
        static_cast<vk::DeviceSize>(model.vertices.size()) * sizeof(Vertex);
    const uint32_t vcount = static_cast<uint32_t>(model.vertices.size());

    const uint32_t icount = static_cast<uint32_t>(model.indices.size());
    const uint32_t* idxPtr = icount ? model.indices.data() : nullptr;

    MeshBuffers mesh = uploadMesh(
        model.vertices.data(), vbytes, vcount,
        idxPtr, icount
    );

    // store in cache
    m_meshes.emplace(path, mesh);
    std::cout << "Mesh Loaded!" << std::endl;
    return mesh;
}

void VulkanRenderer::initObjectFromMesh(Object &obj, const std::string &pipelineName, const std::string &meshPath) {
    MeshBuffers mesh = loadMeshOBJ(meshPath);

    obj.pipelineName = pipelineName;
    obj.mesh         = mesh;
}

void recurseModelNodes(ModelData* meshdata, const aiScene* aiscene, const aiNode* node, const aiMatrix4x4& parentTr, const int level) {
    // accumulate transformations while traversing hierarchy
    aiMatrix4x4 childTr = parentTr * node->mTransformation;
    aiMatrix3x3 normalTr = aiMatrix3x3(childTr); // TODO: full inverse transpose

    // loop through nodes meshes
    for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
        aiMesh* aimesh = aiscene->mMeshes[node->mMeshes[m]];

        // loop through all vertices and record the v/n/uv/tan data with the nodes model applied
        unsigned int faceOffset = meshdata->vertices.size();
        for (unsigned int t = 0; t < aimesh->mNumVertices; ++t) {
            aiVector3D aipnt = childTr * aimesh->mVertices[t];
            aiVector3D ainrm = aimesh->HasNormals() ? normalTr * aimesh->mNormals[t] : aiVector3D(0,0,1);
            aiVector3D aitex = aimesh->HasTextureCoords(0) ? aimesh->mTextureCoords[0][t] : aiVector3D(0,0,0);
            aiVector3D aitan = aimesh->HasTangentsAndBitangents() ? normalTr * aimesh->mTangents[t] : aiVector3D(1,0,0);

            meshdata->vertices.push_back({{aipnt.x, aipnt.y, aipnt.z},
            {ainrm.x, ainrm.y, ainrm.z},
            {aitex.x, aitex.y}});
        }

        // loop through all faces recording indices
        for (unsigned int t=0; t < aimesh->mNumFaces; ++t) {
            aiFace* aiface = &aimesh->mFaces[t];
            for (int i = 2; i < aiface->mNumIndices; ++i) {
                meshdata->matIdx.push_back(aimesh->mMaterialIndex);
                meshdata->indices.push_back(aiface->mIndices[0] + faceOffset);
                meshdata->indices.push_back(aiface->mIndices[i-1] + faceOffset);
                meshdata->indices.push_back(aiface->mIndices[i] + faceOffset);
            }
        }
    }

    // recurse into nodes children
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        recurseModelNodes(meshdata, aiscene, node->mChildren[i], childTr, level+1);
    }
}

bool ModelData::readAssimpFile(const std::string &path, const Matrix4 &M) {
    std::cout << "Reading " << path << std::endl;

    aiMatrix4x4 modelTr(M[0][0], M[1][0], M[2][0], M[3][0],
        M[0][1], M[1][1], M[2][1], M[3][1],
        M[0][2], M[1][2], M[2][2], M[3][2],
        M[0][3], M[1][3], M[2][3], M[3][3]);

    // check if file exists
    std::ifstream find_it(path.c_str());
    if (find_it.fail()) {
        std::cerr << "File not found: " << path << std::endl;
        return false;
    }

    // read file with assimp
    std::cout << "Assimp " << aiGetVersionMajor() << "." << aiGetVersionMinor() << "." << aiGetVersionPatch() << " Reading " << path << std::endl;
    Assimp::Importer imp;
    const aiScene* aiscene = imp.ReadFile(path.c_str(), aiProcess_Triangulate | aiProcess_GenSmoothNormals);

    if (!aiscene) {
        throw std::runtime_error("Error reading file: " + path);
    }
    if (!aiscene->mRootNode) {
        throw std::runtime_error("Scene has no root node: " + path);
    }

    std::cout << "Assimp mNumMeshes: " << aiscene->mNumMeshes << "\n";
    std::cout << "Assimp mNumMaterials: " << aiscene->mNumMaterials << "\n";
    std::cout << "Assimp mNumTextures: " << aiscene->mNumTextures << "\n";

    for (int i = 0; i < aiscene->mNumMaterials; ++i) {
        aiMaterial* mtl = aiscene->mMaterials[i];
        aiString name;
        mtl->Get(AI_MATKEY_NAME, name);
        aiColor3D emit(0.0f, 0.0f, 0.0f);
        aiColor3D diff(0.0f, 0.0f, 0.0f), spec(0.0f, 0.0f, 0.0f);
        float alpha = 20.0f;
        bool he = mtl->Get(AI_MATKEY_COLOR_EMISSIVE, emit);
        bool hd = mtl->Get(AI_MATKEY_COLOR_DIFFUSE, diff);
        bool hs = mtl->Get(AI_MATKEY_COLOR_SPECULAR, spec);
        bool ha = mtl->Get(AI_MATKEY_SHININESS, &alpha, NULL);
        aiColor3D trans;
        bool ht = mtl->Get(AI_MATKEY_COLOR_TRANSPARENT, trans);

        Material nMat;
        if (!emit.IsBlack()) { // i.e, an emitter
            nMat.diffuse = {1,1,1};
            nMat.specular = {0,0,0};
            nMat.shininess = 0.0f;
            nMat.emission = {emit.r, emit.g, emit.b};
            nMat.textureId = -1;
        }
        else {
            Vector3 Kd(0.5f, 0.5f, 0.5f);
            Vector3 Ks(0.03f, 0.03f, 0.03f);
            if (AI_SUCCESS == hd) Kd = Vector3(diff.r, diff.g, diff.b);
            if (AI_SUCCESS == hs) Ks = Vector3(spec.r, spec.g, spec.b);
            nMat.diffuse = {Kd[0],Kd[1],Kd[2]};
            nMat.specular = {Ks[0],Ks[1],Ks[2]};
            nMat.shininess = alpha; // sqrtf(2.0f/2.0f+alpha));
            nMat.emission = {0,0,0};
            nMat.textureId = -1;
        }

        aiString texPath;
        if (AI_SUCCESS == mtl->GetTexture(aiTextureType_DIFFUSE, 0, &texPath)) {
            std::filesystem::path fullPath = path;
            fullPath.replace_filename(texPath.C_Str());
            std::cout << "Texture: " << fullPath << std::endl;
            nMat.textureId = textures.size();
            auto xxx = fullPath.u8string();
            textures.emplace_back(reinterpret_cast<const char*>(xxx.c_str()), xxx.size());
        }

        materials.push_back(nMat);
    }

    recurseModelNodes(this, aiscene, aiscene->mRootNode, modelTr);

    return true;
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

void VulkanRenderer::copyBufferToImage(vk::CommandBuffer cmd, Buffer &staging, Image &img, uint32_t w, uint32_t h, uint32_t baseLayer, uint32_t layerCount) {

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectFromFormat(img.format);
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = baseLayer;
    region.imageSubresource.layerCount = layerCount;
    region.imageOffset = vk::Offset3D{ 0, 0, 0 };
    region.imageExtent = vk::Extent3D{ w, h, 1 };

    cmd.copyBufferToImage(staging.handle, img.handle, vk::ImageLayout::eTransferDstOptimal, 1, &region);
}

vk::Buffer VulkanRenderer::uploadVertexBuffer(const void *data, vk::DeviceSize sizeBytes, uint32_t vertexCount) {
    Buffer dst = createBuffer(sizeBytes,
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
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

void VulkanRenderer::generateMipmaps(vk::CommandBuffer cmd, Image &img) {
    if (img.mipLevels <= 1) return;

    int32_t mipWidth  = static_cast<int32_t>(img.width);
    int32_t mipHeight = static_cast<int32_t>(img.height);

    vk::ImageMemoryBarrier2 barrier{};
    barrier.image = img.handle;
    barrier.subresourceRange.aspectMask = aspectFromFormat(img.format);
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = img.layers;
    barrier.subresourceRange.levelCount = 1;

    vk::DependencyInfo dep{};

    for (uint32_t i = 1; i < img.mipLevels; ++i) {
        // Transition src mip (i-1) from TRANSFER_DST -> TRANSFER_SRC
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout     = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;

        dep.setImageMemoryBarriers(barrier);
        cmd.pipelineBarrier2(dep);

        // Blit i-1 -> i
        vk::ImageBlit2 blit{};
        blit.srcSubresource = { aspectFromFormat(img.format), i - 1, 0, img.layers };
        blit.srcOffsets[0]  = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1]  = vk::Offset3D{mipWidth, mipHeight, 1};
        blit.dstSubresource = { aspectFromFormat(img.format), i, 0, img.layers };
        blit.dstOffsets[0]  = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1]  = vk::Offset3D{
            std::max(mipWidth  / 2, 1),
            std::max(mipHeight / 2, 1),
            1
        };

        vk::BlitImageInfo2 blitInfo{};
        blitInfo.srcImage       = img.handle;
        blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        blitInfo.dstImage       = img.handle;
        blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        blitInfo.regionCount    = 1;
        blitInfo.pRegions       = &blit;
        blitInfo.filter         = vk::Filter::eLinear;

        cmd.blitImage2(blitInfo);

        // Transition src mip (i-1) from TRANSFER_SRC -> SHADER_READ_ONLY
        barrier.oldLayout     = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

        dep.setImageMemoryBarriers(barrier);
        cmd.pipelineBarrier2(dep);

        if (mipWidth > 1)  mipWidth  /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last mip level from TRANSFER_DST -> SHADER_READ_ONLY
    barrier.subresourceRange.baseMipLevel = img.mipLevels - 1;
    barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
    barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

    dep.setImageMemoryBarriers(barrier);
    cmd.pipelineBarrier2(dep);

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
