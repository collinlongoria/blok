/*
* File: renderer_postprocess.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/8/2025
*/

#include "renderer_postprocess.hpp"
#include "image_states.hpp"
#include "renderer.hpp"

namespace blok {

PostProcess::PostProcess(Renderer* r)
    : renderer(r) {
    prevView = glm::mat4(1.0f);
    prevProj = glm::mat4(1.0f);
    prevViewProj = glm::mat4(1.0f);
    initJitterSequence();
}

void PostProcess::init(uint32_t width, uint32_t height) {
    createBuffers(width, height);
    createSamplers();
    createDescriptorSetLayouts();
    allocateDescriptorSets();
    createTAAPipeline();
    createTonemapPipeline();
    createSharpenPipeline();
}

void PostProcess::cleanup() {
    auto& device = renderer->m_device;

    device.waitIdle();

    // Destroy TAA pipeline
    if (pipeline.taaPipeline) {
        device.destroyPipeline(pipeline.taaPipeline);
        pipeline.taaPipeline = nullptr;
    }
    if (pipeline.taaPipelineLayout) {
        device.destroyPipelineLayout(pipeline.taaPipelineLayout);
        pipeline.taaPipelineLayout = nullptr;
    }
    if (pipeline.taaSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.taaSetLayout);
        pipeline.taaSetLayout = nullptr;
    }

    // Destroy tonemap pipeline
    if (pipeline.tonemapPipeline) {
        device.destroyPipeline(pipeline.tonemapPipeline);
        pipeline.tonemapPipeline = nullptr;
    }
    if (pipeline.tonemapPipelineLayout) {
        device.destroyPipelineLayout(pipeline.tonemapPipelineLayout);
        pipeline.tonemapPipelineLayout = nullptr;
    }
    if (pipeline.tonemapSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.tonemapSetLayout);
        pipeline.tonemapSetLayout = nullptr;
    }

    // Destroy sharpen pipeline
    if (pipeline.sharpenPipeline) {
        device.destroyPipeline(pipeline.sharpenPipeline);
        pipeline.sharpenPipeline = nullptr;
    }
    if (pipeline.sharpenPipelineLayout) {
        device.destroyPipelineLayout(pipeline.sharpenPipelineLayout);
        pipeline.sharpenPipelineLayout = nullptr;
    }
    if (pipeline.sharpenSetLayout) {
        device.destroyDescriptorSetLayout(pipeline.sharpenSetLayout);
        pipeline.sharpenSetLayout = nullptr;
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

    destroyBuffers();
}

void PostProcess::resize(uint32_t width, uint32_t height) {
    renderer->m_device.waitIdle();

    destroyBuffers();
    createBuffers(width, height);

    // Reset history since we resized
    hasPreviousFrame = false;
    jitterIndex = 0;
}

void PostProcess::createBuffers(uint32_t width, uint32_t height) {
    // TAA history buffers
    for (int i = 0; i < 2; i++) {
        buffers.taaHistory[i] = renderer->createImage(
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
    }

    // TAA output
    buffers.taaOutput = renderer->createImage(
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

    // Tonemap output
    buffers.tonemapOutput = renderer->createImage(
        width, height,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eTransferSrc,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Sharpen output
    buffers.sharpenOutput = renderer->createImage(
        width, height,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eTransferSrc,
        vk::ImageTiling::eOptimal,
        vk::SampleCountFlagBits::e1,
        1, 1,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    buffers.historyIndex = 0;
}

void PostProcess::destroyBuffers() {
    auto& device = renderer->m_device;
    auto& allocator = renderer->m_allocator;

    auto destroyImage = [&](Image& img) {
        if (img.view) device.destroyImageView(img.view);
        if (img.handle) vmaDestroyImage(allocator, img.handle, img.alloc);
        img = {};
    };

    for (int i = 0; i < 2; i++) {
        destroyImage(buffers.taaHistory[i]);
    }

    destroyImage(buffers.taaOutput);
    destroyImage(buffers.tonemapOutput);
    destroyImage(buffers.sharpenOutput);
}

void PostProcess::createSamplers() {
    // Linear sampler for TAA history sampling
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

    // Nearest sampler for point sampling
    vk::SamplerCreateInfo nearestSci = linearSci;
    nearestSci.magFilter = vk::Filter::eNearest;
    nearestSci.minFilter = vk::Filter::eNearest;

    pipeline.nearestSampler = renderer->m_device.createSampler(nearestSci);
}

void PostProcess::initJitterSequence() {
    // Halton sequence (2, 3) for TAA jitter
    for (int i = 0; i < JITTER_SEQUENCE_LENGTH; ++i) {
        jitterSequence[i].x = halton(i + 1, 2) - 0.5f;  // Center around 0
        jitterSequence[i].y = halton(i + 1, 3) - 0.5f;
    }
}

float PostProcess::halton(int index, int base) {
    float result = 0.0f;
    float f = 1.0f / static_cast<float>(base);
    int i = index;

    while (i > 0) {
        result += f * static_cast<float>(i % base);
        i = i / base;
        f = f / static_cast<float>(base);
    }

    return result;
}

glm::vec2 PostProcess::getJitterOffset() const {
    return jitterSequence[jitterIndex];
}

glm::vec2 PostProcess::getJitterClipSpace(uint32_t width, uint32_t height) const {
    glm::vec2 jitter = getJitterOffset();
    // Convert from pixel offset to clip space
    return glm::vec2(
        (2.0f * jitter.x) / static_cast<float>(width),
        (2.0f * jitter.y) / static_cast<float>(height)
    );
}

void PostProcess::advanceJitter() {
    jitterIndex = (jitterIndex + 1) % JITTER_SEQUENCE_LENGTH;
}

void PostProcess::updatePreviousFrameData(const glm::mat4& view, const glm::mat4& proj) {
    prevView = view;
    prevProj = proj;
    prevViewProj = proj * view;
    hasPreviousFrame = true;
}

glm::mat4 PostProcess::getJitteredProjection(const glm::mat4& proj, uint32_t width, uint32_t height) const {
    if (!settings.enableTAA) {
        return proj;
    }

    glm::vec2 jitter = getJitterClipSpace(width, height);

    glm::mat4 jitteredProj = proj;
    // apply jitter to projection matrix
    // affects NDC position
    jitteredProj[2][0] += jitter.x;
    jitteredProj[2][1] += jitter.y;

    return jitteredProj;
}

void PostProcess::createDescriptorSetLayouts() {
    // TAA Layout
    std::vector<vk::DescriptorSetLayoutBinding> taaBindings = {
        // 0: Current frame color (after denoising)
        {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: Previous TAA history (sampled)
        {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // 2: Motion vectors
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 3: Depth buffer (for depth-based rejection)
        {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 4: Output TAA result
        {4, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 5: Output to history (for next frame)
        {5, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        // 6: Frame UBO
        {6, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo taaCi{};
    taaCi.setBindings(taaBindings);
    pipeline.taaSetLayout = renderer->m_device.createDescriptorSetLayout(taaCi);

    // Tonemap Layout
    std::vector<vk::DescriptorSetLayoutBinding> tonemapBindings = {
        // 0: HDR input
        {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: LDR output
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo tonemapCi{};
    tonemapCi.setBindings(tonemapBindings);
    pipeline.tonemapSetLayout = renderer->m_device.createDescriptorSetLayout(tonemapCi);

    // Sharpen Layout
    std::vector<vk::DescriptorSetLayoutBinding> sharpenBindings = {
        // 0: Input image
        {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        // 1: Output image
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
    };

    vk::DescriptorSetLayoutCreateInfo sharpenCi{};
    sharpenCi.setBindings(sharpenBindings);
    pipeline.sharpenSetLayout = renderer->m_device.createDescriptorSetLayout(sharpenCi);
}

void PostProcess::allocateDescriptorSets() {
    for (uint32_t i = 0; i < PostProcessPipeline::MAX_FRAMES_IN_FLIGHT; ++i) {
        pipeline.taaSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device, pipeline.taaSetLayout);

        pipeline.tonemapSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device, pipeline.tonemapSetLayout);

        pipeline.sharpenSets[i] = renderer->m_descAlloc.allocate(
            renderer->m_device, pipeline.sharpenSetLayout);
    }
}

void PostProcess::updateDescriptorSets(uint32_t frameIndex, Image& inputColor) {
    // TAA Descriptor Set
    {
        vk::DescriptorSet set = pipeline.taaSets[frameIndex];

        vk::DescriptorImageInfo currentColorInfo{nullptr, inputColor.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo prevHistoryInfo{
            pipeline.linearSampler,
            buffers.previousHistory().view,
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        vk::DescriptorImageInfo motionInfo{nullptr, renderer->m_denoiser.gbuffer.motionVectors.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo depthInfo{nullptr, renderer->m_denoiser.gbuffer.worldPosition.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo outputInfo{nullptr, buffers.taaOutput.view, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo historyOutInfo{nullptr, buffers.currentHistory().view, vk::ImageLayout::eGeneral};

        auto& fr = renderer->m_frames[frameIndex];
        vk::DescriptorBufferInfo uboInfo{fr.frameUBO.handle, 0, sizeof(FrameUBO)};

        std::array<vk::WriteDescriptorSet, 7> writes{};
        writes[0] = {set, 0, 0, 1, vk::DescriptorType::eStorageImage, &currentColorInfo};
        writes[1] = {set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &prevHistoryInfo};
        writes[2] = {set, 2, 0, 1, vk::DescriptorType::eStorageImage, &motionInfo};
        writes[3] = {set, 3, 0, 1, vk::DescriptorType::eStorageImage, &depthInfo};
        writes[4] = {set, 4, 0, 1, vk::DescriptorType::eStorageImage, &outputInfo};
        writes[5] = {set, 5, 0, 1, vk::DescriptorType::eStorageImage, &historyOutInfo};
        writes[6] = {set, 6, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uboInfo};

        renderer->m_device.updateDescriptorSets(writes, {});
    }

    // Tonemap Descriptor Set
    // FIX: Use correct input based on whether TAA is enabled
    {
        vk::DescriptorSet set = pipeline.tonemapSets[frameIndex];

        // THIS IS THE FIX: Choose input based on TAA state
        Image& tonemapInput = settings.enableTAA ? buffers.taaOutput : inputColor;

        vk::DescriptorImageInfo inputInfo{
            pipeline.linearSampler,
            tonemapInput.view,  // <-- NOW USES CORRECT INPUT
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        vk::DescriptorImageInfo outputInfo{nullptr, buffers.tonemapOutput.view, vk::ImageLayout::eGeneral};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0] = {set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &inputInfo};
        writes[1] = {set, 1, 0, 1, vk::DescriptorType::eStorageImage, &outputInfo};

        renderer->m_device.updateDescriptorSets(writes, {});
    }

    // Sharpen Descriptor Set
    {
        vk::DescriptorSet set = pipeline.sharpenSets[frameIndex];

        vk::DescriptorImageInfo inputInfo{
            pipeline.linearSampler,
            buffers.tonemapOutput.view,
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        vk::DescriptorImageInfo outputInfo{nullptr, buffers.sharpenOutput.view, vk::ImageLayout::eGeneral};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0] = {set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &inputInfo};
        writes[1] = {set, 1, 0, 1, vk::DescriptorType::eStorageImage, &outputInfo};

        renderer->m_device.updateDescriptorSets(writes, {});
    }
}

void PostProcess::createTAAPipeline() {
    auto shaderModule = renderer->m_shaderManager.loadModule(
        "assets/shaders/taa.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset = 0;
    pushRange.size = sizeof(TAAPushConstants);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.taaSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    pipeline.taaPipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.taaPipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.taaPipeline = result.value;

    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void PostProcess::createTonemapPipeline() {
    auto shaderModule = renderer->m_shaderManager.loadModule(
        "assets/shaders/tonemap.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset = 0;
    pushRange.size = sizeof(TonemapPushConstants);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.tonemapSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    pipeline.tonemapPipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.tonemapPipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.tonemapPipeline = result.value;

    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void PostProcess::createSharpenPipeline() {
    auto shaderModule = renderer->m_shaderManager.loadModule(
        "assets/shaders/sharpen.comp",
        vk::ShaderStageFlagBits::eCompute
    );

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule.module;
    stageInfo.pName = "main";

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset = 0;
    pushRange.size = sizeof(SharpenPushConstants);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.sharpenSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    pipeline.sharpenPipelineLayout = renderer->m_device.createPipelineLayout(layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipeline.sharpenPipelineLayout;

    auto result = renderer->m_device.createComputePipeline(nullptr, pipelineInfo);
    pipeline.sharpenPipeline = result.value;

    renderer->m_device.destroyShaderModule(shaderModule.module);
}

void PostProcess::process(vk::CommandBuffer cmd, Image& inputColor, uint32_t width, uint32_t height, uint32_t frameIndex) {
    ImageTransitions it{cmd};

    // Update descriptor sets with current frame's input
    updateDescriptorSets(frameIndex, inputColor);

    // Memory barrier helper
    auto insertBarrier = [&cmd]() {
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;

        vk::DependencyInfo dep{};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        cmd.pipelineBarrier2(dep);
    };

    // TAA Pass
    if (settings.enableTAA) {
        it.ensure(inputColor, Role::General);
        it.ensure(buffers.previousHistory(), Role::ShaderReadOnly);
        it.ensure(buffers.taaOutput, Role::General);
        it.ensure(buffers.currentHistory(), Role::General);

        dispatchTAA(cmd, inputColor, width, height, frameIndex);
        insertBarrier();
    }

    // Tonemap Pass
    if (settings.enableTonemapping) {
        if (settings.enableTAA) {
            it.ensure(buffers.taaOutput, Role::ShaderReadOnly);
        } else {
            it.ensure(inputColor, Role::ShaderReadOnly);
        }
        it.ensure(buffers.tonemapOutput, Role::General);

        dispatchTonemap(cmd, width, height, frameIndex);
        insertBarrier();
    }

    // Sharpen Pass
    if (settings.enableSharpening && settings.enableTonemapping) {
        it.ensure(buffers.tonemapOutput, Role::ShaderReadOnly);
        it.ensure(buffers.sharpenOutput, Role::General);

        dispatchSharpen(cmd, width, height, frameIndex);
        // Add barrier after sharpen too for safety
        insertBarrier();
    }
}

void PostProcess::dispatchTAA(vk::CommandBuffer cmd, Image& inputColor, uint32_t width, uint32_t height, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.taaPipeline);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.taaPipelineLayout,
        0,
        pipeline.taaSets[frameIndex],
        {}
    );

    // Push constants
    TAAPushConstants pc{};
    glm::vec2 jitter = getJitterOffset();
    pc.jitterX = jitter.x;
    pc.jitterY = jitter.y;
    pc.feedbackMin = settings.feedbackMin;
    pc.feedbackMax = settings.feedbackMax;

    cmd.pushConstants(
        pipeline.taaPipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        sizeof(TAAPushConstants),
        &pc
    );

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

void PostProcess::dispatchTonemap(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.tonemapPipeline);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.tonemapPipelineLayout,
        0,
        pipeline.tonemapSets[frameIndex],
        {}
    );

    TonemapPushConstants pc{};
    pc.exposure = settings.exposure;
    pc.saturationBoost = settings.saturationBoost;
    pc.tonemapOperator = static_cast<int>(settings.tonemapOperator);
    pc.whitePoint = settings.whitePoint;

    cmd.pushConstants(
        pipeline.tonemapPipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        sizeof(TonemapPushConstants),
        &pc
    );

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

void PostProcess::dispatchSharpen(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex) {
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.sharpenPipeline);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipeline.sharpenPipelineLayout,
        0,
        pipeline.sharpenSets[frameIndex],
        {}
    );

    SharpenPushConstants pc{};
    pc.sharpenStrength = settings.sharpenStrength;

    cmd.pushConstants(
        pipeline.sharpenPipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        sizeof(SharpenPushConstants),
        &pc
    );

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmd.dispatch(groupsX, groupsY, 1);
}

Image& PostProcess::getOutputImage() {
    // Return the final output based on which passes are enabled
    if (settings.enableSharpening && settings.enableTonemapping) {
        return buffers.sharpenOutput;
    } else if (settings.enableTonemapping) {
        return buffers.tonemapOutput;
    } else if (settings.enableTAA) {
        return buffers.taaOutput;
    }
    // If nothing is enabled, we need to return the input somehow
    // TODO consider adding a passthrough path
    return buffers.taaOutput;
}

void PostProcess::swapHistoryBuffers() {
    buffers.swapHistory();
    advanceJitter();
}

}