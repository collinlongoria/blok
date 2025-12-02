/*
* File: shader_manager.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef SHADER_MANAGER_HPP
#define SHADER_MANAGER_HPP
#include <string>
#include <unordered_map>
#include "vulkan_context.hpp"

namespace blok {

struct ShaderKey {
    std::string path;
    vk::ShaderStageFlagBits stage;
    bool operator==(const ShaderKey& o) const noexcept { return stage==o.stage && path==o.path; }
};

struct ShaderKeyHash {
    size_t operator()(const ShaderKey& k) const noexcept {
        return std::hash<std::string>{}(k.path) ^ (static_cast<size_t>(k.stage) << 1);
    }
};

struct ShaderModuleEntry {
    std::vector<uint32_t> data;
    vk::ShaderModule module;
};

class ShaderManager {
public:
    explicit ShaderManager(vk::Device device);
    ~ShaderManager();

    const ShaderModuleEntry& loadModule(const std::string& glslPath, vk::ShaderStageFlagBits stage);

private:
    static std::string loadFile(const std::string& path);
    std::vector<uint32_t> compileShader(const std::string& source, vk::ShaderStageFlagBits stage);

    vk::Device m_device{};
    std::unordered_map<ShaderKey, ShaderModuleEntry, ShaderKeyHash> m_cache{};
};

}

#endif //SHADER_MANAGER_HPP