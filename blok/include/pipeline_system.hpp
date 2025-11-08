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

class ShaderSystem;

enum class PipelineKind { Graphics, Compute, RayTracing };

struct PushConstantRangeDesc {
    vk::ShaderStageFlags stages{};
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct PipelineLayoutEntry {
    std::vector<vk::DescriptorSetLayout> setLayouts;
    std::vector<PushConstantRangeDesc> pushConstants;
};

// For dynamic rendering
struct RenderTargetsDesc {
    std::vector<vk::Format> colorFormats;
    std::optional<vk::Format> depthFormat;
};

struct GraphicsStatesDesc {
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    bool depthTest = true;
    bool depthWrite = true;
    vk::CompareOp depthCompare = vk::CompareOp::eLessOrEqual;
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
    bool enableBlend = false;
};

struct ShaderStageDesc {
    std::string path; // GLSL path
    vk::ShaderStageFlagBits stage;
    std::string entry = "main";
};

struct GraphicsPipelineDesc {
    std::string name;
    PipelineLayoutEntry layout;
    RenderTargetsDesc rts;
    GraphicsStatesDesc states;
    // Currently binding0 tightly packed, TODO: can add this to YAML potentially?
    std::vector<vk::Format> vertexAttribFormats;
    uint32_t vertexStride = 0;
    std::vector<ShaderStageDesc> shaders;
};

struct ComputePipelineDesc {
    std::string name;
    PipelineLayoutEntry layout;
    ShaderStageDesc shader;
};

// TODO: Raytracing completion
struct RayTracingPipelineDesc {
    std::string name;
    PipelineLayoutEntry layout;
    ShaderStageDesc rgen; // raygen
    std::vector<ShaderStageDesc> miss; // miss (one or more)
    std::vector<ShaderStageDesc> hit; // anyhit and closesthit
};

struct PipelineProgram {
    PipelineKind kind;
    vk::UniquePipeline pipeline;
    vk::UniquePipelineLayout layout;
    std::vector<vk::DescriptorSetLayout> setLayouts;
};

class PipelineSystem {
public:
    void init(vk::Device device, vk::PhysicalDevice phys, ShaderSystem* shaderSys);
    void shutdown();

    std::vector<std::string> loadPipelinesFromYAML(const std::string& yamlPath);

    const PipelineProgram& get(const std::string& name) const;

private:
    // Builders
    PipelineProgram buildGraphics(const GraphicsPipelineDesc& d);
    PipelineProgram buildCompute(const ComputePipelineDesc& d);
    PipelineProgram buildRayTracing(const RayTracingPipelineDesc& d);

    vk::Device m_device{};
    vk::PhysicalDevice m_phys{};
    ShaderSystem* m_shaders = nullptr;
    vk::UniquePipelineCache m_cache{};
    std::unordered_map<std::string, PipelineProgram> m_programs;
};

}

#endif //BLOK_PIPELINE_SYSTEM_HPP