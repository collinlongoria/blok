/*
* File: shader_manager
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/
#ifndef BLOK_SHADER_SYSTEM_HPP
#define BLOK_SHADER_SYSTEM_HPP

#include <unordered_map>
#include <vulkan/vulkan.hpp>

#include "shader_pipe.hpp"

namespace blok {

struct ShaderKey {
    std::string path; // GLSL path
    vk::ShaderStageFlagBits stage;
    bool operator==(const ShaderKey& o) const noexcept { return stage==o.stage && path==o.path; }
};

struct ShaderKeyHash {
    size_t operator()(const ShaderKey& k) const noexcept {
        // cached by (path, stage)
        return std::hash<std::string>{}(k.path) ^ (static_cast<size_t>(k.stage) << 1);
    }
};

struct ShaderModuleEntry {
    std::vector<uint32_t> spirv;
    vk::UniqueShaderModule module;
};

class ShaderSystem {
public:
    void init(vk::Device device);
    void shutdown();

    const ShaderModuleEntry& loadModule(const std::string& glslPath, vk::ShaderStageFlagBits stage);

private:
    vk::Device m_device{};
    std::unordered_map<ShaderKey, ShaderModuleEntry, ShaderKeyHash> m_cache{};
};

}

#endif //BLOK_SHADER_SYSTEM_HPP