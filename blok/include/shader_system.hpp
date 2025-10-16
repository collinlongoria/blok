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

struct StageKey {
    std::string path;
    shaderpipe::ShaderStage stage;
    shaderpipe::VKVersion target;
    bool operator==(const StageKey& other) const noexcept {
        return path == other.path && stage == other.stage && target == other.target;
    }
};

struct StageKeyHash {
    size_t operator()(const StageKey& k) const noexcept {
        return std::hash<std::string>{}(k.path) ^ (static_cast<size_t>(k.stage) << 1) ^ (static_cast<size_t>(k.target) <<2);
    }
};

struct CompiledStage {
    vk::ShaderModule module{};
    shaderpipe::ShaderReflection reflection{};
};

struct ShaderStageRef {
    StageKey key;
    const char* entry = "main";
};

class ShaderManager {
public:
    explicit ShaderManager(vk::Device device) : m_device(device) {}
    ~ShaderManager() { destroyAll(); }

    CompiledStage get(const StageKey& key);
    void destroyAll();

private:
    vk::Device m_device{};
    std::unordered_map<StageKey, CompiledStage, StageKeyHash> m_cache;
};

}

#endif //BLOK_SHADER_SYSTEM_HPP