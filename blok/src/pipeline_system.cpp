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

// helper to convert shaderpipe stage -> vk stage flag
static vk::ShaderStageFlagBits toVkShaderStage(shaderpipe::ShaderStage s) {
    switch (s) {
    case shaderpipe::ShaderStage::VERTEX: return vk::ShaderStageFlagBits::eVertex;
    case shaderpipe::ShaderStage::FRAGMENT: return vk::ShaderStageFlagBits::eFragment;
    case shaderpipe::ShaderStage::COMPUTE: return vk::ShaderStageFlagBits::eCompute;
    case shaderpipe::ShaderStage::GEOMETRY: return vk::ShaderStageFlagBits::eGeometry;
    case shaderpipe::ShaderStage::TESS_CONTROL: return vk::ShaderStageFlagBits::eTessellationControl;
    case shaderpipe::ShaderStage::TESS_EVAL: return vk::ShaderStageFlagBits::eTessellationEvaluation;
    default: return vk::ShaderStageFlagBits::eAll;
    }
} // TODO: could put this directly in shaderpipe

const BuiltProgram &PipelineManager::getOrCreate(const std::string &name, const PipelineDesc &desc) {
    if (auto it = m_pipelines.find(name); it != m_pipelines.end())
        return it->second;

    // compile/fetch shader modules and collect reflections
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    std::vector<shaderpipe::ShaderReflection> refls;
    stages.reserve(desc.shaders.size());
    refls.reserve(desc.shaders.size());

    for (auto& s : desc.shaders) {
        auto cs = m_shaders.get(s.key);
        refls.push_back(cs.reflection);
        stages.push_back(vk::PipelineShaderStageCreateInfo{
            {}, toVkShaderStage(s.key.stage), cs.module, s.entry
        });
    }

    // merge descriptor set layouts across all stages
    std::unordered_map<uint32_t, SetLayoutKey> mergedPerSet;
    std::vector<BindingInfo> mergedBindings;

    for (auto& r : refls) {
        for (auto& b : r.descriptorBindings) {
            auto& key = mergedPerSet[b.set];
            auto it = std::find_if(key.bindings.begin(), key.bindings.end(),
                [&](const BindingKey& k) { return k.binding == b.binding; });
            if (it == key.bindings.end())
                key.bindings.push_back({b.binding, b.type, b.count, b.stageFlags});
            else
                it->stages |= b.stageFlags;

            // also setting flattened binding info so renderer can write descriptors later
            mergedBindings.push_back({ b.set,b.binding,b.type,b.count,b.stageFlags });
        }
    }

    // compute set count
    size_t setCount = 0;
    for (auto& [set, _] : mergedPerSet)
        setCount = std::max(setCount, static_cast<size_t>(set+ 1));

    std::vector<vk::DescriptorSetLayout> layouts(setCount);
    for (auto& [set, key] : mergedPerSet)
        layouts[set] = m_dsl.get(key);

    // merge push constants
    std::vector<PCRange> pcs;
    for (auto& r : refls) {
        for (auto& p : r.pushConstants)
            pcs.push_back({p.offset, p.size, p.stageFlags});
    }

    // create pipeline layout
    vk::PipelineLayoutCreateInfo pli{};
    pli.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pli.pSetLayouts = layouts.data();
    pli.pushConstantRangeCount = static_cast<uint32_t>(pcs.size());
    std::vector<vk::PushConstantRange> vkpcs;
    vkpcs.reserve(pcs.size());
    for (auto& p : pcs)
        vkpcs.push_back({static_cast<vk::ShaderStageFlags>(p.stageFlags), p.offset, p.size});
    pli.pPushConstantRanges = vkpcs.data();

    BuiltProgram program{};
    program.layout = m_device.createPipelineLayout(pli);
    program.setLayouts = layouts;
    program.bindings = std::move(mergedBindings);
    program.pushConstants = std::move(pcs);
    program.isCompute = desc.isCompute;

    // create correct pipeline
    if (desc.isCompute) {
        vk::ComputePipelineCreateInfo ci{};
        ci.stage = stages.at(0);
        ci.layout = program.layout;
        program.pipeline = m_device.createComputePipeline(m_cache, ci).value;
    }
    else {
        // fixed-function bits from desc.fixed
        vk::PipelineInputAssemblyStateCreateInfo ia{{}, desc.fixed.topology, VK_FALSE};
        vk::PipelineRasterizationStateCreateInfo rs{
            {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, desc.fixed.cull, desc.fixed.front, VK_TRUE, 0.f, 0.f, 0.f, 1.f
        };

        vk::PipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        cba.blendEnable = desc.fixed.blending;
        vk::PipelineColorBlendStateCreateInfo cb{{}, VK_FALSE, vk::LogicOp::eClear, 1, &cba};

       vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dyn{{}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data()};

        vk::PipelineDepthStencilStateCreateInfo ds{};
        ds.depthTestEnable = desc.fixed.depthTest;
        ds.depthWriteEnable = desc.fixed.depthWrite;
        ds.depthCompareOp = vk::CompareOp::eLessOrEqual;

        vk::PipelineVertexInputStateCreateInfo vi{};

        vk::PipelineRenderingCreateInfo rci{};
        rci.colorAttachmentCount = 1;
        rci.pColorAttachmentFormats = &desc.colorFormat;
        if (desc.depthFormat) rci.depthAttachmentFormat = *desc.depthFormat;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.sampleShadingEnable = VK_FALSE;
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;
        ms.minSampleShading = 1.0f;
        ms.pSampleMask = nullptr;
        ms.alphaToCoverageEnable = VK_FALSE;
        ms.alphaToOneEnable = VK_FALSE;

        vk::GraphicsPipelineCreateInfo gp{};
        gp.pNext = &rci;
        gp.stageCount = static_cast<uint32_t>(stages.size());
        gp.pStages = stages.data();
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pMultisampleState = &ms;
        gp.pDynamicState = &dyn;
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
