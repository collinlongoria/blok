/*
* File: renderer_temporal_reprojection.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "renderer_denoising.hpp"

#include "image_states.hpp"
#include "renderer.hpp"

namespace blok {

Denoiser::Denoiser(Renderer *r)
    : renderer(r) {
    prevView = glm::mat4(1.0f);
    prevProj = glm::mat4(1.0f);
    prevViewProj = glm::mat4(1.0f);
    prevCamPos = glm::vec3(0.0f);
}

void Denoiser::init(uint32_t width, uint32_t height) {
    createGBuffer(width, height);
    createSamplers();
    createDescriptorSetLayouts();
    allocateDescriptorSets();
    createTemporalPipeline();
    createVariancePipeline();
    createAtrousPipeline();

    // Initialize all per-frame descriptor sets
    for (uint32_t i = 0; i < DenoiserPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        updateDescriptorSets(i);
    }
}

void Denoiser::cleanup() {
    auto& device = renderer->m_device;
    auto& allocator = renderer->m_allocator;

    device.waitIdle();

    // Destroy pipelines
    if (pipeline.temporalPipeline) {
        device.destroyPipeline(pipeline.temporalPipeline);
        pipeline.temporalPipeline = nullptr;
    }
    if (pipeline.temporalPipelineLayout) {
        device.destroyPipelineLayout(pipeline.temporalPipelineLayout);
        pipeline.temporalPipelineLayout = nullptr;
    }
    if (pipeline.temporalSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.temporalSetLayout);
        pipeline.temporalSetLayout = nullptr;
    }

    if (pipeline.variancePipeline) {
        device.destroyPipeline(pipeline.variancePipeline);
        pipeline.variancePipeline = nullptr;
    }
    if (pipeline.variancePipelineLayout) {
        device.destroyPipelineLayout(pipeline.variancePipelineLayout);
        pipeline.variancePipelineLayout = nullptr;
    }
    if (pipeline.varianceSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.varianceSetLayout);
        pipeline.varianceSetLayout = nullptr;
    }

    if (pipeline.atrousPipeline) {
        device.destroyPipeline(pipeline.atrousPipeline);
        pipeline.atrousPipeline = nullptr;
    }
    if (pipeline.atrousPipelineLayout) {
        device.destroyPipelineLayout(pipeline.atrousPipelineLayout);
        pipeline.atrousPipelineLayout = nullptr;
    }
    if (pipeline.atrousSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.atrousSetLayout);
        pipeline.atrousSetLayout = nullptr;
    }

    // Destroy samplers
    if (pipeline.linearSampler) {
        device.destroySampler(pipeline.linearSampler);
        pipeline.linearSampler = nullptr;
    }
    if (pipeline.nearestSampler) {
        device.destroySampler(pipeline.nearestSampler);
        pipeline.nearestSampler = nullptr;
    }

    destroyGBuffer();
}

void Denoiser::resize(uint32_t width, uint32_t height) {
    renderer->m_device.waitIdle();

    destroyGBuffer();
    createGBuffer(width, height);

    for (uint32_t i = 0; i < DenoiserPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        updateDescriptorSets(i);
    }

    // Reset history since we resized
    hasPreviousFrame = false;
}

void Denoiser::createGBuffer(uint32_t width, uint32_t height) {
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

    // motion vectors
    gbuffer.motionVectors = renderer->createImage(
        width, height,
        vk::Format::eR16G16Sfloat,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Double-buffered history
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

        gbuffer.historyLength[i] = renderer->createImage(
            width, height,
            vk::Format::eR16Sfloat,
            vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled,
            vk::ImageTiling::eOptimal,
            vk::SampleCountFlagBits::e1,
            1, 1,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );
    }

    // Variance buffer
    gbuffer.variance = renderer->createImage(
        width, height,
        vk::Format::eR32Sfloat,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Ping-pong buffers for à-trous filtering
    gbuffer.filterPing = renderer->createImage(
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

    gbuffer.filterPong = renderer->createImage(
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

    gbuffer.historyIndex = 0;
}

void Denoiser::destroyGBuffer() {
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
    destroyImage(gbuffer.motionVectors);

    for (int i = 0; i < 2; i++) {
        destroyImage(gbuffer.historyColor[i]);
        destroyImage(gbuffer.historyMoments[i]);
        destroyImage(gbuffer.historyLength[i]);
    }

    destroyImage(gbuffer.variance);
    destroyImage(gbuffer.filterPing);
    destroyImage(gbuffer.filterPong);
}

void Denoiser::createSamplers() {
    // Linear sampler for filtered lookups
    vk::SamplerCreateInfo linearSci{};
    linearSci.magFilter = vk::Filter::eLinear;
    linearSci.minFilter = vk::Filter::eLinear;
    linearSci.mipmapMode = vk::SamplerMipmapMode::eNearest;
    linearSci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    linearSci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    linearSci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    linearSci.mipLodBias = 0.0f;
    linearSci.anisotropyEnable = VK_FALSE;
    linearSci.compareEnable = VK_FALSE;
    linearSci.minLod = 0.0f;
    linearSci.maxLod = 0.0f;
    linearSci.borderColor = vk::BorderColor::eFloatOpaqueBlack;
    linearSci.unnormalizedCoordinates = VK_FALSE;

    pipeline.linearSampler = renderer->m_device.createSampler(linearSci);

    // Nearest sampler for point lookups (G-buffer data)
    vk::SamplerCreateInfo nearestSci = linearSci;
    nearestSci.magFilter = vk::Filter::eNearest;
    nearestSci.minFilter = vk::Filter::eNearest;

    pipeline.nearestSampler = renderer->m_device.createSampler(nearestSci);
}

void Denoiser::createDescriptorSetLayouts() {
    std::vector<vk::DescriptorSetLayoutBinding> temporalBindings = {
        // 0: Current noisy color
        {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: World position + depth
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 2: Normal + roughness
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 3: Motion vectors
        {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 4: Previous history color (sampled)
        {4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // 5: Previous moments
        {5, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 6: Previous history length
        {6, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 7: Previous world position (for geometry validation)
        {7, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 8: Previous normal (for geometry validation)
        {8, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 9: Output accumulated color
        {9, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 10: Output moments
        {10, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 11: Output history length
        {11, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 12: Frame UBO
        {12, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo temporalCi{};
    temporalCi.setBindings(temporalBindings);
    pipeline.temporalSetLayout = renderer->m_device.createDescriptorSetLayout(temporalCi);

    // Variance Layout
    std::vector<vk::DescriptorSetLayoutBinding> varianceBindings = {
        // 0: Accumulated color (input for spatial 3x3 variance)
        {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: Moments
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 2: History length
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 3: Output variance
        {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 4: Frame UBO
        {4, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo varianceCi{};
    varianceCi.setBindings(varianceBindings);
    pipeline.varianceSetLayout = renderer->m_device.createDescriptorSetLayout(varianceCi);

    // Atrous Filter Layout
    std::vector<vk::DescriptorSetLayoutBinding> atrousBindings = {
        // 0: Input color (ping or pong)
        {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: Variance
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 2: World position + depth
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 3: Normal + roughness
        {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 4: Output color (pong or ping)
        {4, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 5: Frame UBO
        {5, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo atrousCi{};
    atrousCi.setBindings(atrousBindings);
    pipeline.atrousSetLayout = renderer->m_device.createDescriptorSetLayout(atrousCi);
}

void Denoiser::allocateDescriptorSets() {
    for (uint32_t i = 0; i < DenoiserPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        pipeline.temporalSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device, pipeline.temporalSetLayout);

        pipeline.varianceSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device, pipeline.varianceSetLayout);

        pipeline.atrousSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device, pipeline.atrousSetLayout);
    }
}

void Denoiser::updateDescriptorSets(uint32_t frameIndex) {
    // Temporal Accumulation Descriptor Set
    {
        vk::DescriptorSet set = pipeline.temporalSets[frameIndex];

        // Current frame inputs
        vk::DescriptorImageInfo currentColorInfo{nullptr, gbuffer.color.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo worldPosInfo{nullptr, gbuffer.worldPosition.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo normalInfo{nullptr, gbuffer.normalRoughness.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo motionInfo{nullptr, gbuffer.motionVectors.view, vk::ImageLayout::eGeneral};

        // Previous frame inputs (sampled for interpolation)
        vk::DescriptorImageInfo prevColorInfo{
            pipeline.linearSampler,
            gbuffer.previousHistory().view,
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        vk::DescriptorImageInfo prevMomentsInfo{nullptr, gbuffer.previousMoments().view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo prevHistLenInfo{nullptr, gbuffer.previousHistoryLength().view, vk::ImageLayout::eGeneral};

        // TODO double-buffer the G-buffer too
        vk::DescriptorImageInfo prevWorldPosInfo{nullptr, gbuffer.worldPosition.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo prevNormalInfo{nullptr, gbuffer.normalRoughness.view, vk::ImageLayout::eGeneral};

        // Outputs
        vk::DescriptorImageInfo outColorInfo{nullptr, gbuffer.currentHistory().view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo outMomentsInfo{nullptr, gbuffer.currentMoments().view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo outHistLenInfo{nullptr, gbuffer.currentHistoryLength().view, vk::ImageLayout::eGeneral};

        // UBO
        auto& fr = renderer->m_frames[frameIndex];
        vk::DescriptorBufferInfo uboInfo{fr.frameUBO.handle, 0, sizeof(FrameUBO)};

        std::array<vk::WriteDescriptorSet, 13> writes{};

        writes[0] = {set, 0, 0, 1, vk::DescriptorType::eStorageImage, &currentColorInfo};
        writes[1] = {set, 1, 0, 1, vk::DescriptorType::eStorageImage, &worldPosInfo};
        writes[2] = {set, 2, 0, 1, vk::DescriptorType::eStorageImage, &normalInfo};
        writes[3] = {set, 3, 0, 1, vk::DescriptorType::eStorageImage, &motionInfo};
        writes[4] = {set, 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &prevColorInfo};
        writes[5] = {set, 5, 0, 1, vk::DescriptorType::eStorageImage, &prevMomentsInfo};
        writes[6] = {set, 6, 0, 1, vk::DescriptorType::eStorageImage, &prevHistLenInfo};
        writes[7] = {set, 7, 0, 1, vk::DescriptorType::eStorageImage, &prevWorldPosInfo};
        writes[8] = {set, 8, 0, 1, vk::DescriptorType::eStorageImage, &prevNormalInfo};
        writes[9] = {set, 9, 0, 1, vk::DescriptorType::eStorageImage, &outColorInfo};
        writes[10] = {set, 10, 0, 1, vk::DescriptorType::eStorageImage, &outMomentsInfo};
        writes[11] = {set, 11, 0, 1, vk::DescriptorType::eStorageImage, &outHistLenInfo};
        writes[12] = {set, 12, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uboInfo};

        renderer->m_device.updateDescriptorSets(writes, {});
    }

    // Variance Estimation Descriptor Set
    {
        vk::DescriptorSet set = pipeline.varianceSets[frameIndex];

        vk::DescriptorImageInfo colorInfo{nullptr, gbuffer.color.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo momentsInfo{nullptr, gbuffer.currentMoments().view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo histLenInfo{nullptr, gbuffer.currentHistoryLength().view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo varianceInfo{nullptr, gbuffer.variance.view, vk::ImageLayout::eGeneral};

        auto& fr = renderer->m_frames[frameIndex];
        vk::DescriptorBufferInfo uboInfo{fr.frameUBO.handle, 0, sizeof(FrameUBO)};

        std::array<vk::WriteDescriptorSet, 5> writes{};
        writes[0] = {set, 0, 0, 1, vk::DescriptorType::eStorageImage, &colorInfo};
        writes[1] = {set, 1, 0, 1, vk::DescriptorType::eStorageImage, &momentsInfo};
        writes[2] = {set, 2, 0, 1, vk::DescriptorType::eStorageImage, &histLenInfo};
        writes[3] = {set, 3, 0, 1, vk::DescriptorType::eStorageImage, &varianceInfo};
        writes[4] = {set, 4, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uboInfo};

        renderer->m_device.updateDescriptorSets(writes, {});
    }

    // Atrous Filter Descriptor Set
    {
        vk::DescriptorSet set = pipeline.atrousSets[frameIndex];

        // read from currentHistory write to filterPing
        vk::DescriptorImageInfo inputInfo{
            pipeline.linearSampler,
            gbuffer.currentHistory().view,
            vk::ImageLayout::eGeneral
        };
        vk::DescriptorImageInfo varianceInfo{nullptr, gbuffer.variance.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo worldPosInfo{nullptr, gbuffer.worldPosition.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo normalInfo{nullptr, gbuffer.normalRoughness.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo outputInfo{nullptr, gbuffer.filterPing.view, vk::ImageLayout::eGeneral};

        auto& fr = renderer->m_frames[frameIndex];
        vk::DescriptorBufferInfo uboInfo{fr.frameUBO.handle, 0, sizeof(FrameUBO)};

        std::array<vk::WriteDescriptorSet, 6> writes{};
        writes[0] = {set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &inputInfo};
        writes[1] = {set, 1, 0, 1, vk::DescriptorType::eStorageImage, &varianceInfo};
        writes[2] = {set, 2, 0, 1, vk::DescriptorType::eStorageImage, &worldPosInfo};
        writes[3] = {set, 3, 0, 1, vk::DescriptorType::eStorageImage, &normalInfo};
        writes[4] = {set, 4, 0, 1, vk::DescriptorType::eStorageImage, &outputInfo};
        writes[5] = {set, 5, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uboInfo};

        renderer->m_device.updateDescriptorSets(writes, {});
    }
}

void Denoiser::createTemporalPipeline() {
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
    layoutInfo.pSetLayouts = &pipeline.temporalSetLayout;

    pipeline.temporalPipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.temporalPipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.temporalPipeline = result.value;

    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void Denoiser::createVariancePipeline() {
    auto shaderModule = renderer->m_shaderManager.loadModule(
        "assets/shaders/variance.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.varianceSetLayout;

    pipeline.variancePipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.variancePipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.variancePipeline = result.value;

    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void Denoiser::createAtrousPipeline() {
    auto shaderModule = renderer->m_shaderManager.loadModule(
        "assets/shaders/atrous.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    // Push constants for step size
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset = 0;
    pushRange.size = sizeof(AtrousPC);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.atrousSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    pipeline.atrousPipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.atrousPipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.atrousPipeline = result.value;

    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void Denoiser::updatePreviousFrameData(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos) {
    prevView = view;
    prevProj = proj;
    prevViewProj = proj * view;
    prevCamPos = camPos;
    hasPreviousFrame = true;
}

void Denoiser::fillFrameUBO(
    FrameUBO& ubo,
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& camPos,
    float deltaTime,
    int depth,
    uint32_t frameCount,
    uint32_t screenWidth,
    uint32_t screenHeight,
    int atrousIteration
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
    ubo.momentAlpha = settings.momentAlpha;
    ubo.varianceClipGamma = settings.varianceClipGamma;
    ubo.depthThreshold = settings.depthThreshold;

    ubo.phiColor = settings.phiColor;
    ubo.phiNormal = settings.phiNormal;
    ubo.phiDepth = settings.phiDepth;
    ubo.varianceBoost = settings.varianceBoost;

    ubo.minHistoryLength = settings.minHistoryLength;
    ubo.stepSize = (1 << atrousIteration);
}

Image& Denoiser::getOutputImage() {
    if (settings.atrousIterations % 2 == 1) {
        return gbuffer.filterPong;
    } else {
        return gbuffer.filterPing;
    }
}

void Denoiser::denoise(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex) {
    ImageTransitions it{cmd};

    // Temporal Accumulation
    // Transition inputs to readable, outputs to writable
    it.ensure(gbuffer.color, Role::General);
    it.ensure(gbuffer.worldPosition, Role::General);
    it.ensure(gbuffer.normalRoughness, Role::General);
    it.ensure(gbuffer.motionVectors, Role::General);
    it.ensure(gbuffer.previousHistory(), Role::ShaderReadOnly);
    it.ensure(gbuffer.previousMoments(), Role::General);
    it.ensure(gbuffer.previousHistoryLength(), Role::General);
    it.ensure(gbuffer.currentHistory(), Role::General);
    it.ensure(gbuffer.currentMoments(), Role::General);
    it.ensure(gbuffer.currentHistoryLength(), Role::General);

    dispatchTemporalAccumulation(cmd, width, height, frameIndex);

    // Memory barrier: temporal to variance
    vk::MemoryBarrier2 barrier{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;

    vk::DependencyInfo dep{};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    cmd.pipelineBarrier2(dep);

    // Variance
    it.ensure(gbuffer.variance, Role::General);

    dispatchVarianceEstimation(cmd, width, height, frameIndex);

    // Memory barrier: variance to atrous
    cmd.pipelineBarrier2(dep);

    // Atrous Wavelet Filtering
    it.ensure(gbuffer.filterPing, Role::General);
    it.ensure(gbuffer.filterPong, Role::General);

    for (int i = 0; i < settings.atrousIterations; ++i) {
        dispatchAtrousFilter(cmd, width, height, frameIndex, i);

        // Barrier between iterations
        if (i < settings.atrousIterations - 1) {
            cmd.pipelineBarrier2(dep);
        }
    }
}

void Denoiser::dispatchTemporalAccumulation(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.temporalPipeline);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.temporalPipelineLayout,
        0,
        pipeline.temporalSets[frameIndex],
        {}
    );

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

void Denoiser::dispatchVarianceEstimation(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.variancePipeline);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.variancePipelineLayout,
        0,
        pipeline.varianceSets[frameIndex],
        {}
    );

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

void Denoiser::dispatchAtrousFilter(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex, int iteration) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.atrousPipeline);

    Image* inputImage;
    Image* outputImage;

    if (iteration == 0) {
        inputImage = &gbuffer.currentHistory();
        outputImage = &gbuffer.filterPing;
    } else if (iteration % 2 == 1) {
        inputImage = &gbuffer.filterPing;
        outputImage = &gbuffer.filterPong;
    } else {
        inputImage = &gbuffer.filterPong;
        outputImage = &gbuffer.filterPing;
    }

    // Update descriptor set for this iteration
    vk::DescriptorImageInfo inputInfo{
        pipeline.linearSampler,
        inputImage->view,
        vk::ImageLayout::eGeneral
    };
    vk::DescriptorImageInfo outputInfo{nullptr, outputImage->view, vk::ImageLayout::eGeneral};

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0] = {pipeline.atrousSets[frameIndex], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &inputInfo};
    writes[1] = {pipeline.atrousSets[frameIndex], 4, 0, 1, vk::DescriptorType::eStorageImage, &outputInfo};

    renderer->m_device.updateDescriptorSets(writes, {});

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.atrousPipelineLayout,
        0,
        pipeline.atrousSets[frameIndex],
        {}
    );

    // Push constants with step size
    AtrousPC pc{};
    pc.stepSize = 1 << iteration;
    pc.phiColor = settings.phiColor;
    pc.phiNormal = settings.phiNormal;
    pc.phiDepth = settings.phiDepth;

    cmd.pushConstants(
        pipeline.atrousPipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        sizeof(AtrousPC),
        &pc
    );

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

void Denoiser::swapHistoryBuffers() {
    gbuffer.swapHistory();
}

}