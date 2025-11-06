/*
* File: pipeline_manager
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/

#include <algorithm>
#include <iostream>
#include <array>

#include <yaml-cpp/yaml.h>

#include "pipeline_system.hpp"
#include "shader_system.hpp"

using namespace blok;

static vk::ShaderStageFlagBits toStage(const std::string& s) {
    if (s == "vertex") return vk::ShaderStageFlagBits::eVertex;
    if (s == "fragment") return vk::ShaderStageFlagBits::eFragment;
    if (s == "geometry") return vk::ShaderStageFlagBits::eGeometry;
    if (s == "compute") return vk::ShaderStageFlagBits::eCompute;
    if (s == "raygen") return vk::ShaderStageFlagBits::eRaygenKHR;
    if (s == "miss") return vk::ShaderStageFlagBits::eMissKHR;
    if (s == "anyhit") return vk::ShaderStageFlagBits::eAnyHitKHR;
    if (s == "closesthit") return vk::ShaderStageFlagBits::eClosestHitKHR;

    return vk::ShaderStageFlagBits::eAll; // TODO: No clue what this actually is. Fix this later to include all cases.
}

static vk::DescriptorType toDescType(const std::string& s) {
    if (s == "sampler") return vk::DescriptorType::eSampler;
    if (s == "combined_image_sampler") return vk::DescriptorType::eCombinedImageSampler;
    if (s == "sampled_image") return vk::DescriptorType::eSampledImage;
    if (s == "storage_image") return vk::DescriptorType::eStorageImage;
    if (s == "uniform_texel_buffer") return vk::DescriptorType::eUniformTexelBuffer;
    if (s == "storage_texel_buffer") return vk::DescriptorType::eStorageTexelBuffer;
    if (s == "uniform_buffer") return vk::DescriptorType::eUniformBuffer;
    if (s == "storage_buffer") return vk::DescriptorType::eStorageBuffer;
    if (s == "uniform_buffer_dynamic") return vk::DescriptorType::eUniformBufferDynamic;
    if (s == "storage_buffer_dynamic") return vk::DescriptorType::eStorageBufferDynamic;
    if (s == "input_attachment") return vk::DescriptorType::eInputAttachment;

    return vk::DescriptorType::eSampler; // TODO: This one should maybe also not be this
}

static vk::ShaderStageFlags stagesFromNode(const YAML::Node& node) {
    vk::ShaderStageFlags flags{};
    for (auto s : node) flags |= toStage(s.as<std::string>());
    return flags;
}

void PipelineSystem::init(vk::Device device, vk::PhysicalDevice phys, ShaderSystem *shaderSys) {
    m_device = device; m_phys = phys; m_shaders = shaderSys;
    vk::PipelineCacheCreateInfo ci{}; m_cache = m_device.createPipelineCacheUnique(ci);
}

void PipelineSystem::shutdown() {
    for (auto& [name, prog] : m_programs) {
        for (auto layout : prog.setLayouts) {
            if (layout)
                m_device.destroyDescriptorSetLayout(layout);
        }
    }

    m_programs.clear();
    m_cache.reset();
}

const PipelineProgram &PipelineSystem::get(const std::string &name) const {
    auto it = m_programs.find(name); if (it == m_programs.end()) throw std::runtime_error("pipeline not found: "+name);
    return it->second;
}

// TODO: Change YAML to include names that can get translated to vulkan types
// TODO: increase verbosity and readability of YAML
std::vector<std::string> PipelineSystem::loadPipelinesFromYAML(const std::string &yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);
    std::vector<std::string> created;
    for (const auto& n : root) {
        const std::string kind = n["kind"].as<std::string>();
        if (kind == "graphics") {
            GraphicsPipelineDesc d{}; d.name = n["name"].as<std::string>();
            // formats
            for (auto f: n["rts"]["colors"]) d.rts.colorFormats.push_back(static_cast<vk::Format>(f.as<int>()));
            if (n["rts"]["depth"]) d.rts.depthFormat = static_cast<vk::Format>(n["rts"]["depth"].as<int>());
            // layout: push constants
            if (auto pc = n["layout"]["push_constants"]) {
                for (auto p: pc) {
                    PushConstantRangeDesc pr{};
                    pr.offset = p["offset"].as<uint32_t>();
                    pr.size = p["size"].as<uint32_t>();
                    vk::ShaderStageFlags stages{}; for (auto s : p["stages"]) stages |= toStage(s.as<std::string>());
                    pr.stages = stages; d.layout.pushConstants.push_back(pr);
                }
            }
            // layout: descriptor sets
            if (auto sets = n["layout"]["sets"]) {
                // collect all sets
                std::map<int, std::vector<vk::DescriptorSetLayoutBinding>> bySet;

                for (auto s : sets) {
                    int setIdx = s["set"].as<int>();
                    std::vector<vk::DescriptorSetLayoutBinding> bindings;
                    if (auto bs = s["bindings"]) {
                        for (auto b : bs) {
                            vk::DescriptorSetLayoutBinding l{};
                            l.binding = b["binding"].as<uint32_t>();
                            l.descriptorType = toDescType(b["type"].as<std::string>());
                            l.descriptorCount = 1;
                            l.stageFlags = stagesFromNode(b["stages"]);
                            bindings.push_back(l);
                        }
                    }
                    bySet[setIdx] = std::move(bindings);
                }

                d.layout.setLayouts.clear();
                for (auto& [setIdx, bindings] : bySet) {
                    vk::DescriptorSetLayoutCreateInfo lci{};
                    lci.setBindings(bindings);
                    auto layout = m_device.createDescriptorSetLayout(lci);
                    d.layout.setLayouts.push_back(layout);
                }
            }
            // vertex
            d.vertexStride = n["vertex"]["stride"].as<uint32_t>();
            for (auto a: n["vertex"]["attrib_formats"]) d.vertexAttribFormats.push_back(static_cast<vk::Format>(a.as<uint32_t>()));
            // shaders
            for (auto s: n["shaders"]) d.shaders.push_back({s["path"].as<std::string>(), toStage(s["stage"].as<std::string>()), s["entry"].as<std::string>("main")});
            // states
            if (auto st = n["states"]) {
                d.states.depthTest = st["depthTest"].as<bool>(true);
                d.states.depthWrite = st["depthWrite"].as<bool>(true);
                d.states.enableBlend = st["blend"].as<bool>(false);
            }
            m_programs.emplace(d.name, buildGraphics(d));
            created.push_back(d.name);
        }
        else if (kind == "compute") {
            ComputePipelineDesc d{};
            d.name = n["name"].as<std::string>();

            // layout: push constants
            if (auto pc = n["layout"]["push_constants"]) {
                for (auto p : pc) {
                    PushConstantRangeDesc pr{};
                    pr.offset = p["offset"].as<uint32_t>();
                    pr.size = p["size"].as<uint32_t>();
                    vk::ShaderStageFlags stages{};
                    for (auto s : p["stages"]) stages |= toStage(s.as<std::string>());
                    pr.stages = stages;
                    d.layout.pushConstants.push_back(pr);
                }
            }

            // layout: descriptor sets
            if (auto sets = n["layout"]["sets"]) {
                // collect all sets
                std::map<int, std::vector<vk::DescriptorSetLayoutBinding>> bySet;

                for (auto s : sets) {
                    int setIdx = s["set"].as<int>();
                    std::vector<vk::DescriptorSetLayoutBinding> bindings;
                    if (auto bs = s["bindings"]) {
                        for (auto b : bs) {
                            vk::DescriptorSetLayoutBinding l{};
                            l.binding = b["binding"].as<uint32_t>();
                            l.descriptorType = toDescType(b["type"].as<std::string>());
                            l.descriptorCount = 1;
                            l.stageFlags = stagesFromNode(b["stages"]);
                            bindings.push_back(l);
                        }
                    }
                    bySet[setIdx] = std::move(bindings);
                }

                d.layout.setLayouts.clear();
                for (auto& [setIdx, bindings] : bySet) {
                    vk::DescriptorSetLayoutCreateInfo lci{};
                    lci.setBindings(bindings);
                    auto layout = m_device.createDescriptorSetLayout(lci);
                    d.layout.setLayouts.push_back(layout);
                }
            }

            // shader
            const auto& s = n["shader"];
            d.shader = { s["path"].as<std::string>(), toStage(s["stage"].as<std::string>()), s["entry"].as<std::string>("main")};
            m_programs.emplace(d.name, buildCompute(d));
            created.push_back(d.name);
        }
        else if (kind == "raytracing") {}
    }
    return created;
}

PipelineProgram PipelineSystem::buildGraphics(const GraphicsPipelineDesc &d) {
    // layout
    std::vector<vk::PushConstantRange> pcr;
    for (auto& p : d.layout.pushConstants) pcr.push_back({p.stages, p.offset, p.size});
    vk::PipelineLayoutCreateInfo lci{}; lci.setSetLayouts(d.layout.setLayouts).setPushConstantRanges(pcr);
    auto layout = m_device.createPipelineLayoutUnique(lci);

    // shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    std::vector<vk::UniqueShaderModule> owned; owned.reserve(d.shaders.size());
    for (auto& s : d.shaders) {
        const auto& sm = m_shaders->loadModule(s.path, s.stage);
        stages.push_back(vk::PipelineShaderStageCreateInfo({}, s.stage, sm.module.get(), s.entry.c_str()));
    }

    // vertex input
    auto formatSize = [](vk::Format f)->uint32_t {
        switch (f) {
            case vk::Format::eR32G32B32Sfloat: return 12;
            case vk::Format::eR32G32Sfloat:    return 8;
            case vk::Format::eR32Sfloat:       return 4;
            case vk::Format::eR8G8B8A8Unorm:   return 4;
            default: return 0; // add as needed
        }
    };

    std::vector<vk::VertexInputAttributeDescription> attrs;
    attrs.reserve(d.vertexAttribFormats.size());
    uint32_t offset = 0;
    for (uint32_t i = 0; i < d.vertexAttribFormats.size(); ++i) {
        const auto fmt = d.vertexAttribFormats[i];
        attrs.push_back(vk::VertexInputAttributeDescription{
            i,              // location
            0,              // binding
            fmt,            // format
            offset          // offset
        });
        offset += formatSize(fmt);
    }
    vk::VertexInputBindingDescription bind{0, d.vertexStride, vk::VertexInputRate::eVertex};
    vk::PipelineVertexInputStateCreateInfo vi{}; vi.setVertexBindingDescriptions(bind).setVertexAttributeDescriptions(attrs);

    // input assembly
    vk::PipelineInputAssemblyStateCreateInfo ia{}; ia.setTopology(d.states.topology);

    // rasterization
    vk::PipelineRasterizationStateCreateInfo rs{}; rs.setCullMode(d.states.cullMode).setFrontFace(d.states.frontFace).setPolygonMode(vk::PolygonMode::eFill).setLineWidth(1.0f);

    // multisample and depth
    vk::PipelineMultisampleStateCreateInfo ms{}; ms.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineDepthStencilStateCreateInfo ds{}; ds.setDepthTestEnable(d.states.depthTest).setDepthWriteEnable(d.states.depthWrite).setDepthCompareOp(d.states.depthCompare);

    // color blend
    vk::PipelineColorBlendAttachmentState cba{}; cba.setColorWriteMask(vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB|vk::ColorComponentFlagBits::eA);
    if (d.states.enableBlend) cba.setBlendEnable(true);
    vk::PipelineColorBlendStateCreateInfo cb{}; cb.setAttachments(cba);

    // viewport state
    vk::PipelineViewportStateCreateInfo vp{};
    vp.setViewportCount(1).setScissorCount(1);

    // viewport and scissor are dynamic
    std::array<vk::DynamicState,2> dynStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dyn{}; dyn.setDynamicStates(dynStates);

    // dynamic rendering info
    std::vector<vk::Format> colorFmts = d.rts.colorFormats;
    vk::PipelineRenderingCreateInfo pci{}; pci.setColorAttachmentFormats(colorFmts);
    if (d.rts.depthFormat) pci.setDepthAttachmentFormat(*d.rts.depthFormat);

    vk::GraphicsPipelineCreateInfo gci{};
    gci.setPNext(&pci)
    .setStages(stages)
    .setPVertexInputState(&vi)
    .setPInputAssemblyState(&ia)
    .setPViewportState(&vp)
    .setPRasterizationState(&rs)
    .setPMultisampleState(&ms)
    .setPDepthStencilState(d.rts.depthFormat ? &ds : nullptr)
    .setPColorBlendState(&cb)
    .setPDynamicState(&dyn)
    .setLayout(layout.get());

    auto pipe = m_device.createGraphicsPipelineUnique(m_cache.get(), gci).value;
    std::cout << "Graphics pipeline created!" << std::endl;
    return {PipelineKind::Graphics, std::move(pipe), std::move(layout), d.layout.setLayouts};
}

PipelineProgram PipelineSystem::buildCompute(const ComputePipelineDesc &d) {
    std::vector<vk::PushConstantRange> pcr; for (auto& p : d.layout.pushConstants) pcr.push_back({p.stages, p.offset, p.size});
    vk::PipelineLayoutCreateInfo lci{}; lci.setSetLayouts(d.layout.setLayouts).setPushConstantRanges(pcr);

    auto layout = m_device.createPipelineLayoutUnique(lci);
    const auto& sm = m_shaders->loadModule(d.shader.path, d.shader.stage);
    vk::ComputePipelineCreateInfo ci{}; ci.setStage({{}, d.shader.stage, sm.module.get(), d.shader.entry.c_str()}).setLayout(layout.get());

    auto pipe = m_device.createComputePipelineUnique(m_cache.get(), ci).value;
    std::cout << "Compute pipeline created!" << std::endl;
    return {PipelineKind::Compute, std::move(pipe), std::move(layout), d.layout.setLayouts};
}

// TODO: implement this function
PipelineProgram PipelineSystem::buildRayTracing(const RayTracingPipelineDesc &d) {
    PipelineProgram program{};

    return program;
}
