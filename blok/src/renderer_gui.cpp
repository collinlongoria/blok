/*
* File: renderer_gui
* Project: blok
* Author: Collin Longoria
* Created on: 12/1/2025
*
* Description:
*/
#include "renderer.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

namespace blok {

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

}