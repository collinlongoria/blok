/*
* File: shader_manager
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/

#include "shader_system.hpp"

using namespace blok;

// helper to convert to to shaderpipe enums
static shaderpipe::ShaderStage vk_to_pipe(vk::ShaderStageFlagBits vk) {
    switch (vk) {
        case vk::ShaderStageFlagBits::eVertex: return shaderpipe::ShaderStage::VERTEX;
        case vk::ShaderStageFlagBits::eFragment: return shaderpipe::ShaderStage::FRAGMENT;
        case vk::ShaderStageFlagBits::eCompute: return shaderpipe::ShaderStage::COMPUTE;
        case vk::ShaderStageFlagBits::eGeometry: return shaderpipe::ShaderStage::GEOMETRY;
        case vk::ShaderStageFlagBits::eTessellationControl: return shaderpipe::ShaderStage::TESS_CONTROL;
        case vk::ShaderStageFlagBits::eTessellationEvaluation: return shaderpipe::ShaderStage::TESS_EVAL;
        case vk::ShaderStageFlagBits::eCallableKHR: return shaderpipe::ShaderStage::CALLABLE;
        case vk::ShaderStageFlagBits::eRaygenKHR: return shaderpipe::ShaderStage::RAYGEN;
        case vk::ShaderStageFlagBits::eAnyHitKHR: return shaderpipe::ShaderStage::ANY_HIT;
        case vk::ShaderStageFlagBits::eClosestHitKHR: return shaderpipe::ShaderStage::CLOSEST_HIT;
        case vk::ShaderStageFlagBits::eIntersectionKHR: return shaderpipe::ShaderStage::INTERSECT;
        case vk::ShaderStageFlagBits::eMissKHR: return shaderpipe::ShaderStage::MISS;
        default: return shaderpipe::ShaderStage::VERTEX; // TODO: missing some
    }
}

void ShaderSystem::init(vk::Device device) {
    m_device = device;
}

void ShaderSystem::shutdown() {
    m_cache.clear();
}

const ShaderModuleEntry &ShaderSystem::loadModule(const std::string &glslPath, vk::ShaderStageFlagBits stage) {
    ShaderKey key{glslPath, stage};
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    ShaderModuleEntry ent{};
    ent.spirv = shaderpipe::glsl_to_spirv(shaderpipe::load_shader_file(glslPath), vk_to_pipe(stage), shaderpipe::VKVersion::VK_1_4);
    vk::ShaderModuleCreateInfo ci{};
    ci.setCodeSize(ent.spirv.size()*sizeof(uint32_t)).setPCode(ent.spirv.data());
    ent.module = m_device.createShaderModuleUnique(ci);
    auto [ins, _] = m_cache.emplace(key, std::move(ent));
    return ins->second;
}
