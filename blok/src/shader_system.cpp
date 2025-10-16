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

CompiledStage ShaderManager::get(const StageKey &key) {
    if (auto it = m_cache.find(key); it != m_cache.end()) return it->second;

    auto src = shaderpipe::load_shader_file(key.path);
    auto built = shaderpipe::glsl_to_spirv_with_reflection(src, key.stage, key.target);

    vk::ShaderModuleCreateInfo ci{};
    ci.codeSize = built.spirv.size() * sizeof(uint32_t);
    ci.pCode = built.spirv.data();

    CompiledStage out;
    out.module = m_device.createShaderModule(ci);
    out.reflection = std::move(built.reflection); // TODO: potential optimization from noexcept
    m_cache.emplace(key, out);
    return out;
}

void ShaderManager::destroyAll() {
    for (auto& [_,v] : m_cache)
        if (v.module) m_device.destroyShaderModule(v.module);
    m_cache.clear();
}
