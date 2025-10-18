/*
* File: pipeline_manager
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/
#ifndef BLOK_PIPELINE_SYSTEM_HPP
#define BLOK_PIPELINE_SYSTEM_HPP

#include <optional>

#include "shader_system.hpp"
#include "descriptor_system.hpp"

namespace blok {

struct GraphicsFixedFunc {
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    bool depthTest = false;
    bool depthWrite = false;
    bool blending = false;
    vk::CullModeFlags cull = vk::CullModeFlagBits::eNone;
    vk::FrontFace front = vk::FrontFace::eCounterClockwise;
};

struct PipelineDesc {
    std::vector<ShaderStageRef> shaders;
    GraphicsFixedFunc fixed{};
    vk::Format colorFormat{};
    std::optional<vk::Format> depthFormat{};
    bool isCompute = false;
};

struct BindingInfo {
    uint32_t set;
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    VkShaderStageFlags stageFlags;
};

struct PCRange {
    uint32_t offset;
    uint32_t size;
    VkShaderStageFlags stageFlags;
};

struct BuiltProgram {
    vk::PipelineLayout layout{};
    vk::Pipeline pipeline{};
    std::vector<vk::DescriptorSetLayout> setLayouts;
    std::vector<BindingInfo> bindings;
    std::vector<PCRange> pushConstants;
    bool isCompute = false;
};

class PipelineManager {
public:
    PipelineManager(vk::Device d, vk::PipelineCache c, ShaderManager& sm, DescriptorSetLayoutCache& dc)
        : m_device(d), m_cache(c), m_shaders(sm), m_dsl(dc) {}

    const BuiltProgram& getOrCreate(const std::string& name, const PipelineDesc& desc);
    void destroyAll();

private:
    vk::Device m_device;
    vk::PipelineCache m_cache;
    ShaderManager& m_shaders;
    DescriptorSetLayoutCache& m_dsl;

    std::unordered_map<std::string, BuiltProgram> m_pipelines;
};

}

#endif //BLOK_PIPELINE_SYSTEM_HPP