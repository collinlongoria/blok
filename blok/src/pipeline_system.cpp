/*
* File: pipeline_manager
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/

#include <algorithm>

#include "pipeline_system.hpp"
#include "shader_system.hpp"

using namespace blok;

const BuiltProgram &PipelineManager::getOrCreate(const std::string &name, const PipelineDesc &desc) {
    if (auto it = m_pipelines.find(name); it != m_pipelines.end()) {}

    // compile shaders
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    std::vector<shaderpipe::ShaderReflection> refls;

    for (auto& s : desc.shaders) {
        auto stage = m_shaders.get(s.key);
        refls.push_back(stage.reflection);
        stages.push_back({
            {},
            static_cast<vk::ShaderStageFlagBits>(s.key.stage), // TODO: this will not work, need to actually convert
            stage.module,
            s.entry
        });
    }

    // descriptor layouts
    std::unordered_map<uint32_t, SetLayoutKey> merged;
    for (auto& r : refls) {
        for (auto& b : r.descriptorBindings) {
            auto& key = merged[b.set];
            auto it = std::find_if(key.bindings.begin(), key.bindings.end(),
                [&](auto& bk){ return bk.binding == b.binding; });
            if (it == key.bindings.end())
                key.bindings.push_back({b.binding, b.type, b.count, b.stageFlags});
            else
                it->stages |= b.stageFlags;
        }
    }

    size_t setCount = 0;
    for (auto& [set,_] : merged) setCount = std::max(setCount, static_cast<size_t>(set + 1));

    std::vector<vk::DescriptorSetLayout> layouts(setCount);
    for (auto& [set, key] : merged)
        layouts[set] = m_dsl.get(key);

    // push constants
    std::vector<vk::PushConstantRange> pcs;
    for (auto& r : refls)
        for (auto& p : r.pushConstants)
            pcs.push_back({static_cast<vk::ShaderStageFlags>(p.stageFlags), p.offset, p.size});

    // pipeline layout
    vk::PipelineLayoutCreateInfo pli{};
    pli.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pli.pSetLayouts = layouts.data();
    pli.pushConstantRangeCount = static_cast<uint32_t>(pcs.size());
    pli.pPushConstantRanges = pcs.data();

    BuiltProgram program{};
    program.layout = m_device.createPipelineLayout(pli);
    program.setLayouts = layouts;

    if (desc.isCompute) /* Compute Pipeline */ {
        vk::ComputePipelineCreateInfo ci{};
        ci.stage = stages[0];
        ci.layout = program.layout;
        program.pipeline = m_device.createComputePipeline(m_cache, ci).value;
    }
    else /* Graphics Pipeline */ {
        vk::PipelineInputAssemblyStateCreateInfo ia{{}, desc.fixed.topology};
        vk::PipelineRasterizationStateCreateInfo rs{{}, VK_FALSE, VK_FALSE,
            vk::PolygonMode::eFill, desc.fixed.cull, desc.fixed.front};
        vk::PipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = vk::ColorComponentFlagBits::eR |
                             vk::ColorComponentFlagBits::eG |
                             vk::ColorComponentFlagBits::eB |
                             vk::ColorComponentFlagBits::eA;
        cba.blendEnable = desc.fixed.blending;

        vk::PipelineColorBlendStateCreateInfo cb{{}, false, {}, 1, &cba};
        vk::PipelineViewportStateCreateInfo vp{};
        vk::PipelineDynamicStateCreateInfo dyn{{}, 2,
            std::array{vk::DynamicState::eViewport, vk::DynamicState::eScissor}.data()};

        vk::PipelineDepthStencilStateCreateInfo ds{};
        ds.depthTestEnable = desc.fixed.depthTest;
        ds.depthWriteEnable = desc.fixed.depthWrite;

        vk::PipelineRenderingCreateInfo renderCI{};
        renderCI.colorAttachmentCount = 1;
        renderCI.pColorAttachmentFormats = &desc.colorFormat;
        if (desc.depthFormat) renderCI.depthAttachmentFormat = *desc.depthFormat;

        vk::GraphicsPipelineCreateInfo gp{};
        gp.pNext = &renderCI;
        gp.stageCount = (uint32_t)stages.size();
        gp.pStages = stages.data();
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.pDepthStencilState = &ds;
        gp.layout = program.layout;
        program.pipeline = m_device.createGraphicsPipeline(m_cache, gp).value;
    }

    return m_pipelines.emplace(name, std::move(program)).first->second;
}

void PipelineManager::destroyAll() {
    for (auto& [_, p] : m_pipelines) {
        if (p.pipeline) m_device.destroyPipeline(p.pipeline);
        if (p.layout) m_device.destroyPipelineLayout(p.layout);
    }
    m_pipelines.clear();
}
