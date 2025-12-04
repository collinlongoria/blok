/*
* File: renderer_gui.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/1/2025
*/
#include "renderer.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

namespace blok {

static constexpr int HISTORY_SIZE = 120;

static float g_fps = 0;
static float g_ms = 0.f;
std::array<float, HISTORY_SIZE> fpsHistory{};
std::array<float, HISTORY_SIZE> frameTimeHistory{};
static float fpsMin = 0.0f, fpsMax = 60.0f;
static float frameTimeMin = 0.0f, frameTimeMax = 16.67f;
float frame_count = 0.f;
float total_time = 0.f;

void Renderer::createGui() {
    // core stuff
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // hook window
    ImGui_ImplGlfw_InitForVulkan(m_window, true);

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

    m_guiDescriptorPool = m_device.createDescriptorPool(pool_info);

    // init imgui
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_4;
    init_info.Instance       = static_cast<VkInstance>(m_instance);
    init_info.PhysicalDevice = static_cast<VkPhysicalDevice>(m_physicalDevice);
    init_info.Device         = static_cast<VkDevice>(m_device);
    init_info.QueueFamily    = *m_qfi.graphics;
    init_info.Queue          = static_cast<VkQueue>(m_graphicsQueue);
    init_info.DescriptorPool = static_cast<VkDescriptorPool>(m_guiDescriptorPool);
    init_info.MinImageCount  = static_cast<uint32_t>(m_swapImages.size());
    init_info.ImageCount     = static_cast<uint32_t>(m_swapImages.size());
    init_info.PipelineInfoMain.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator      = nullptr;
    init_info.CheckVkResultFn = nullptr;
    init_info.UseDynamicRendering = VK_TRUE;

    static VkFormat colorFormat;
    colorFormat = static_cast<VkFormat>(m_colorFormat);

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
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

void Renderer::destroyGui() {
    // imgui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_guiDescriptorPool) {
        m_device.destroyDescriptorPool(m_guiDescriptorPool);
        m_guiDescriptorPool = nullptr;
    }
}

void Renderer::updatePerformanceData(float fps, float ms) {
    // Shift history and add new values
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
        fpsHistory[i] = fpsHistory[i + 1];
        frameTimeHistory[i] = frameTimeHistory[i + 1];
    }
    fpsHistory[HISTORY_SIZE - 1] = fps;
    frameTimeHistory[HISTORY_SIZE - 1] = ms;

    // Update min/max for scaling
    fpsMin = fpsMax = fpsHistory[0];
    frameTimeMin = frameTimeMax = frameTimeHistory[0];
    for (int i = 1; i < HISTORY_SIZE; i++) {
        if (fpsHistory[i] < fpsMin) fpsMin = fpsHistory[i];
        if (fpsHistory[i] > fpsMax) fpsMax = fpsHistory[i];
        if (frameTimeHistory[i] < frameTimeMin) frameTimeMin = frameTimeHistory[i];
        if (frameTimeHistory[i] > frameTimeMax) frameTimeMax = frameTimeHistory[i];
    }

    frame_count++;
    total_time += (ms / 1000);
}

void Renderer::renderPerformanceData() {
    ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav;

    // top left corner
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);

    ImGui::Begin("##PerformancePanel", nullptr, flags);

    const ImVec2 graphSize(200.0f, 50.0f);

    // fps
    float currentFps = fpsHistory[HISTORY_SIZE - 1];
    char fpsOverlay[32];
    snprintf(fpsOverlay, sizeof(fpsOverlay), "FPS: %.1f", currentFps);

    ImGui::PlotLines("##FPS", fpsHistory.data(), HISTORY_SIZE, 0, fpsOverlay,
                     fpsMin * 0.9f, fpsMax * 1.1f, graphSize);

    ImGui::SameLine();

    ImGui::Text("Average FPS: %.1f", (frame_count / total_time));

    ImGui::Spacing();

    // frame time
    float currentFrameTime = frameTimeHistory[HISTORY_SIZE - 1];
    char frameTimeOverlay[32];
    snprintf(frameTimeOverlay, sizeof(frameTimeOverlay), "Frame: %.2f ms", currentFrameTime);

    ImGui::PlotLines("##FrameTime", frameTimeHistory.data(), HISTORY_SIZE, 0, frameTimeOverlay,
                     frameTimeMin * 0.9f, frameTimeMax * 1.1f, graphSize);

    ImGui::End();
}

}