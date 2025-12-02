/*
* File: shader_manager.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "shader_manager.hpp"

#include <fstream>
#include <sstream>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Include/ResourceLimits.h>

static const TBuiltInResource DefaultTBuiltInResource = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,
    /* .maxMeshOutputVerticesEXT = */ 256,
    /* .maxMeshOutputPrimitivesEXT = */ 256,
    /* .maxMeshWorkGroupSizeX_EXT = */ 128,
    /* .maxMeshWorkGroupSizeY_EXT = */ 128,
    /* .maxMeshWorkGroupSizeZ_EXT = */ 128,
    /* .maxTaskWorkGroupSizeX_EXT = */ 128,
    /* .maxTaskWorkGroupSizeY_EXT = */ 128,
    /* .maxTaskWorkGroupSizeZ_EXT = */ 128,
    /* .maxMeshViewCountEXT = */ 4,
    /* .maxDualSourceDrawBuffersEXT = */ 1,

    /* .limits = */ {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
    }
};

namespace blok {

static EShLanguage vkShaderStageToGslang(vk::ShaderStageFlagBits s) {
    switch (s) {
    default:
    case vk::ShaderStageFlagBits::eVertex:                 return EShLangVertex;
    case vk::ShaderStageFlagBits::eTessellationControl:    return EShLangTessControl;
    case vk::ShaderStageFlagBits::eTessellationEvaluation: return EShLangTessEvaluation;
    case vk::ShaderStageFlagBits::eGeometry:               return EShLangGeometry;
    case vk::ShaderStageFlagBits::eFragment:               return EShLangFragment;
    case vk::ShaderStageFlagBits::eCompute:                return EShLangCompute;
    case vk::ShaderStageFlagBits::eRaygenKHR:              return EShLangRayGen;
    case vk::ShaderStageFlagBits::eIntersectionKHR:        return EShLangIntersect;
    case vk::ShaderStageFlagBits::eAnyHitKHR:              return EShLangAnyHit;
    case vk::ShaderStageFlagBits::eClosestHitKHR:          return EShLangClosestHit;
    case vk::ShaderStageFlagBits::eMissKHR:                return EShLangMiss;
    case vk::ShaderStageFlagBits::eCallableKHR:            return EShLangCallable;
    case vk::ShaderStageFlagBits::eTaskEXT:                return EShLangTask;
    case vk::ShaderStageFlagBits::eMeshEXT:                return EShLangMesh;
    } // not sure what else to default to
};

ShaderManager::ShaderManager(vk::Device device)
    : m_device(device) {
    glslang::InitializeProcess();
}

ShaderManager::~ShaderManager() {
    glslang::FinalizeProcess();
    m_cache.clear();
}

std::string ShaderManager::loadFile(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Error opening file: " + path);
    }

    std::stringstream buff;
    buff << file.rdbuf();

    file.close();
    return buff.str();
}

const ShaderModuleEntry &ShaderManager::loadModule(const std::string &glslPath, vk::ShaderStageFlagBits stage) {
    ShaderKey key{glslPath, stage};
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    ShaderModuleEntry ent{};
    std::string src = loadFile(glslPath);
    ent.data = compileShader(src, stage);
    vk::ShaderModuleCreateInfo ci{};
    ci.setCodeSize(ent.data.size()*sizeof(uint32_t)).setPCode(ent.data.data());
    ent.module = m_device.createShaderModule(ci);
    auto [ins, _] = m_cache.emplace(key, std::move(ent));
    return ins->second;
}

std::vector<uint32_t> ShaderManager::compileShader(const std::string &source, vk::ShaderStageFlagBits stage) {
    const char* glslSource = source.c_str();

    auto sStage = vkShaderStageToGslang(stage);
    glslang::TShader shader(sStage);
    shader.setStrings(&glslSource, 1);

    int glslVersion = 460;
    const auto vulkanVersion = glslang::EShTargetVulkan_1_4;
    const auto spvVersion = glslang::EShTargetSpv_1_6;

    shader.setEnvInput(glslang::EShSourceGlsl, sStage, glslang::EShClientVulkan, glslVersion);
    shader.setEnvClient(glslang::EShClientVulkan, vulkanVersion);
    shader.setEnvTarget(glslang::EShTargetSpv, spvVersion);

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(&DefaultTBuiltInResource, 460, false, messages)) {
        throw std::runtime_error(shader.getInfoLog());
    }

    glslang::TProgram program{};
    program.addShader(&shader);

    if (!program.link(EShMsgDefault)) {
        throw std::runtime_error(program.getInfoLog());
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(sStage), spirv);

    return spirv;
}

}