/*
* File: pipeline_manager
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/

#include <algorithm>

#include <yaml-cpp/yaml.h>

#include "pipeline_system.hpp"
#include "shader_system.hpp"

using namespace blok;

void PipelineSystem::init(vk::Device device, vk::PhysicalDevice phys, ShaderSystem *shaderSys, DescriptorSystem *descSys) {
    m_device = device; m_phys = phys; m_shaders = shaderSys; m_desc = descSys;
    vk::PipelineCacheCreateInfo ci{}; m_cache = m_device.createPipelineCacheUnique(ci);
}

void PipelineSystem::shutdown() {
    m_programs.clear();
    m_cache.reset();
}

const PipelineProgram &PipelineSystem::get(const std::string &name) const {
    auto it = m_programs.find(name); if (it == m_programs.end()) throw std::runtime_error("pipeline not found: "+name);
    return it->second;
}

std::vector<std::string> PipelineSystem::loadPipelinesFromYAML(const std::string &yamlPath) {
    return std::vector<std::string>();
}

PipelineProgram PipelineSystem::buildGraphics(const GraphicsPipelineDesc &d) {
    PipelineProgram program{};

    return program;
}

PipelineProgram PipelineSystem::buildCompute(const ComputePipelineDesc &d) {
    PipelineProgram program{};

    return program;
}

PipelineProgram PipelineSystem::buildRayTracing(const RayTracingPipelineDesc &d) {
    PipelineProgram program{};

    return program;
}
