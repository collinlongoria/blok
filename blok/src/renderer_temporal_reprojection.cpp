/*
* File: renderer_temporal_reprojection.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "renderer_temporal_reprojection.hpp"
#include "renderer.hpp"

namespace blok {

TemporalReprojection::TemporalReprojection(Renderer *r)
    : renderer(r) {
    prevView = glm::mat4(1.0f);
    prevProj = glm::mat4(1.0f);
    prevViewProj = glm::mat4(1.0f);
    prevCamPos = glm::vec3(0.0f);
}

void TemporalReprojection::init(uint32_t width, uint32_t height) {
    createGBuffer(width, height);
    createHistoryBuffers(width, height);
    createSampler();
    createDescriptorSetLayout();
    allocateDescriptorSet();
    createPipeline();
    // Initialize all per-frame descriptor sets
    for (uint32_t i = 0; i < TemporalPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        updateDescriptorSet(i);
    }
}

void TemporalReprojection::cleanup() {
    auto& device = renderer->m_device;
    auto& allocator = renderer->m_allocator;

    device.waitIdle();

    if (pipeline.pipeline) {
        device.destroyPipeline(pipeline.pipeline);
        pipeline.pipeline = nullptr;
    }
    if (pipeline.pipelineLayout) {
        device.destroyPipelineLayout(pipeline.pipelineLayout);
        pipeline.pipelineLayout = nullptr;
    }
    if (pipeline.descriptorSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.descriptorSetLayout);
        pipeline.descriptorSetLayout = nullptr;
    }
    if (pipeline.historySampler) {
        device.destroySampler(pipeline.historySampler);
        pipeline.historySampler = nullptr;
    }

    destroyGBuffer();
    destroyHistoryBuffers();
}

void TemporalReprojection::resize(uint32_t width, uint32_t height) {
    renderer->m_device.waitIdle();

    destroyGBuffer();
    destroyHistoryBuffers();

    createGBuffer(width, height);
    createHistoryBuffers(width, height);

    for (uint32_t i = 0; i < TemporalPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        updateDescriptorSet(i);
    }

    // Reset history since we resized
    hasPreviousFrame = false;
}

void TemporalReprojection::createGBuffer(uint32_t width, uint32_t height) {
    // color buffer (raw output)
    gbuffer.color = renderer->createImage(
        width, height,
        vk::Format::eR32G32B32A32Sfloat,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eTransferSrc,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // world position XYZ + depth W
    gbuffer.worldPosition = renderer->createImage(
        width, height,
        vk::Format::eR32G32B32A32Sfloat,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // normal XYZ + roughness W
    gbuffer.normalRoughness = renderer->createImage(
        width, height,
        vk::Format::eR16G16B16A16Sfloat,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // albedo XYZ + metallic W
    gbuffer.albedoMetallic = renderer->createImage(
        width, height,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );
}

void TemporalReprojection::destroyGBuffer() {
    auto& device = renderer->m_device;
    auto& allocator = renderer->m_allocator;

    auto destroyImage = [&](Image& img) {
        if (img.view) device.destroyImageView(img.view);
        if (img.handle) vmaDestroyImage(allocator, img.handle, img.alloc);
        img = {};
    };

    destroyImage(gbuffer.color);
    destroyImage(gbuffer.worldPosition);
    destroyImage(gbuffer.normalRoughness);
    destroyImage(gbuffer.albedoMetallic);
}

void TemporalReprojection::createHistoryBuffers(uint32_t width, uint32_t height) {
    for (int i = 0; i < 2; i++) {
        gbuffer.historyColor[i] = renderer->createImage(
            width, height,
            vk::Format::eR32G32B32A32Sfloat,
            vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eTransferDst,
            vk::ImageTiling::eOptimal,
            vk::SampleCountFlagBits::e1,
            1, 1,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        gbuffer.historyMoments[i] = renderer->createImage(
            width, height,
            vk::Format::eR32G32Sfloat,
            vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled,
            vk::ImageTiling::eOptimal,
            vk::SampleCountFlagBits::e1,
            1, 1,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );
    }

    gbuffer.historyIndex = 0;
}

void TemporalReprojection::destroyHistoryBuffers() {
    auto& device = renderer->m_device;
    auto& allocator = renderer->m_allocator;

    auto destroyImage = [&](Image& img) {
        if (img.view) device.destroyImageView(img.view);
        if (img.handle) vmaDestroyImage(allocator, img.handle, img.alloc);
        img = {};
    };

    for (int i = 0; i < 2; i++) {
        destroyImage(gbuffer.historyColor[i]);
        destroyImage(gbuffer.historyMoments[i]);
    }
}

void TemporalReprojection::createSampler() {
    vk::SamplerCreateInfo sci{};
    sci.magFilter = vk::Filter::eLinear;
    sci.minFilter = vk::Filter::eLinear;
    sci.mipmapMode = vk::SamplerMipmapMode::eNearest;
    sci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    sci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    sci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    sci.mipLodBias = 0.0f;
    sci.anisotropyEnable = VK_FALSE;
    sci.compareEnable = VK_FALSE;
    sci.minLod = 0.0f;
    sci.maxLod = 0.0f;
    sci.borderColor = vk::BorderColor::eFloatOpaqueBlack;
    sci.unnormalizedCoordinates = VK_FALSE;

    pipeline.historySampler = renderer->m_device.createSampler(sci);
}

void TemporalReprojection::createDescriptorSetLayout() {
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        // 0: inColor (current frame noisy)
        {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: inWorldPosition
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 2: inNormalRoughness
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 3: historyColor (sampled)
        {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // 4: historyWorldPosition
        {4, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 5: historyNormalRoughness
        {5, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 6: historyMoments
        {6, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 7: outColor
        {7, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 8: outMoments
        {8, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 9: FrameUBO
        {9, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo ci{};
    ci.setBindings(bindings);

    pipeline.descriptorSetLayout = renderer->m_device.createDescriptorSetLayout(ci);
}

void TemporalReprojection::allocateDescriptorSet() {
    for (uint32_t i = 0; i < TemporalPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        pipeline.descriptorSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device,
            pipeline.descriptorSetLayout
        );
    }
}

void TemporalReprojection::updateDescriptorSet(uint32_t frameIndex) {
    vk::DescriptorSet currentSet = pipeline.descriptorSets[frameIndex];

    // Image infos for storage images (all use General layout)
    vk::DescriptorImageInfo inColorInfo{
        nullptr,
        gbuffer.color.view,
        vk::ImageLayout::eGeneral
    };

    vk::DescriptorImageInfo inWorldPosInfo{
        nullptr,
        gbuffer.worldPosition.view,
        vk::ImageLayout::eGeneral
    };

    vk::DescriptorImageInfo inNormalInfo{
        nullptr,
        gbuffer.normalRoughness.view,
        vk::ImageLayout::eGeneral
    };

    // History color uses sampler with ShaderReadOnlyOptimal
    vk::DescriptorImageInfo historyColorInfo{
        pipeline.historySampler,
        gbuffer.previousHistory().view,
        vk::ImageLayout::eShaderReadOnlyOptimal
    };

    // These are read as storage images, so General layout
    vk::DescriptorImageInfo historyWorldPosInfo{
        nullptr,
        gbuffer.worldPosition.view,  // We reuse current G-buffer as "history" geometry
        vk::ImageLayout::eGeneral
    };

    vk::DescriptorImageInfo historyNormalInfo{
        nullptr,
        gbuffer.normalRoughness.view,  // We reuse current G-buffer as "history" geometry
        vk::ImageLayout::eGeneral
    };

    vk::DescriptorImageInfo historyMomentsInfo{
        nullptr,
        gbuffer.previousMoments().view,
        vk::ImageLayout::eGeneral
    };

    vk::DescriptorImageInfo outColorInfo{
        nullptr,
        gbuffer.currentHistory().view,
        vk::ImageLayout::eGeneral
    };

    vk::DescriptorImageInfo outMomentsInfo{
        nullptr,
        gbuffer.currentMoments().view,
        vk::ImageLayout::eGeneral
    };

    // UBO
    auto& fr = renderer->m_frames[renderer->m_frameIndex];
    vk::DescriptorBufferInfo uboInfo{
        fr.frameUBO.handle,
        0,
        sizeof(FrameUBO)
    };

    // Build write descriptors
    std::array<vk::WriteDescriptorSet, 10> writes{};

    // 0: inColor
    writes[0].dstSet = currentSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = vk::DescriptorType::eStorageImage;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &inColorInfo;

    // 1: inWorldPosition
    writes[1].dstSet = currentSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = vk::DescriptorType::eStorageImage;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &inWorldPosInfo;

    // 2: inNormalRoughness
    writes[2].dstSet = currentSet;
    writes[2].dstBinding = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorType = vk::DescriptorType::eStorageImage;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &inNormalInfo;

    // 3: historyColor (sampled)
    writes[3].dstSet = currentSet;
    writes[3].dstBinding = 3;
    writes[3].dstArrayElement = 0;
    writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &historyColorInfo;

    // 4: historyWorldPosition
    writes[4].dstSet = currentSet;
    writes[4].dstBinding = 4;
    writes[4].dstArrayElement = 0;
    writes[4].descriptorType = vk::DescriptorType::eStorageImage;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &historyWorldPosInfo;

    // 5: historyNormalRoughness
    writes[5].dstSet = currentSet;
    writes[5].dstBinding = 5;
    writes[5].dstArrayElement = 0;
    writes[5].descriptorType = vk::DescriptorType::eStorageImage;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &historyNormalInfo;

    // 6: historyMoments
    writes[6].dstSet = currentSet;
    writes[6].dstBinding = 6;
    writes[6].dstArrayElement = 0;
    writes[6].descriptorType = vk::DescriptorType::eStorageImage;
    writes[6].descriptorCount = 1;
    writes[6].pImageInfo = &historyMomentsInfo;

    // 7: outColor
    writes[7].dstSet = currentSet;
    writes[7].dstBinding = 7;
    writes[7].dstArrayElement = 0;
    writes[7].descriptorType = vk::DescriptorType::eStorageImage;
    writes[7].descriptorCount = 1;
    writes[7].pImageInfo = &outColorInfo;

    // 8: outMoments
    writes[8].dstSet = currentSet;
    writes[8].dstBinding = 8;
    writes[8].dstArrayElement = 0;
    writes[8].descriptorType = vk::DescriptorType::eStorageImage;
    writes[8].descriptorCount = 1;
    writes[8].pImageInfo = &outMomentsInfo;

    // 9: FrameUBO
    writes[9].dstSet = currentSet;
    writes[9].dstBinding = 9;
    writes[9].dstArrayElement = 0;
    writes[9].descriptorType = vk::DescriptorType::eUniformBuffer;
    writes[9].descriptorCount = 1;
    writes[9].pBufferInfo = &uboInfo;

    renderer->m_device.updateDescriptorSets(writes, {});
}

void TemporalReprojection::createPipeline() {
    // Load compute shader
    auto shaderModule = renderer->m_shaderManager.loadModule(
        "assets/shaders/temporal_reproject.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.descriptorSetLayout;

    pipeline.pipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.pipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.pipeline = result.value;

    // Destroy shader module
    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void TemporalReprojection::updatePreviousFrameData(
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& camPos
) {
    prevView = view;
    prevProj = proj;
    prevViewProj = proj * view;
    prevCamPos = camPos;
    hasPreviousFrame = true;
}

void TemporalReprojection::fillFrameUBO(
    FrameUBO& ubo,
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& camPos,
    float deltaTime,
    int depth,
    uint32_t frameCount,
    uint32_t screenWidth,
    uint32_t screenHeight
) {
    ubo.view = view;
    ubo.proj = proj;
    ubo.invView = glm::inverse(view);
    ubo.invProj = glm::inverse(proj);

    ubo.prevView = hasPreviousFrame ? prevView : view;
    ubo.prevProj = hasPreviousFrame ? prevProj : proj;
    ubo.prevViewProj = hasPreviousFrame ? prevViewProj : (proj * view);

    ubo.camPos = camPos;
    ubo.delta_time = deltaTime;

    ubo.prevCamPos = hasPreviousFrame ? prevCamPos : camPos;
    ubo.depth = depth;

    ubo.frame_count = frameCount;
    ubo.sample_count = 1;  // Typically 1 sample per pixel when using temporal accumulation
    ubo.screen_width = screenWidth;
    ubo.screen_height = screenHeight;

    ubo.temporalAlpha = settings.temporalAlpha;
    ubo.momentAlpha = settings.momentAlpha;
    ubo.varianceClipGamma = settings.varianceClipGamma;
    ubo.depthThreshold = settings.depthThreshold;

    ubo.normalThreshold = settings.normalThreshold;
    ubo.padding1 = 0.0f;
    ubo.padding2 = 0.0f;
    ubo.padding3 = 0.0f;
}

void TemporalReprojection::dispatch(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex) {
    // Bind pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline);

    // Bind descriptor set
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.pipelineLayout,
        0,
        pipeline.descriptorSets[frameIndex],
        {}
    );

    // Dispatch - 8x8 workgroup size
    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;

    cmd.dispatch(groupsX, groupsY, 1);
}

void TemporalReprojection::swapHistoryBuffers() {
    gbuffer.swapHistory();
}

}