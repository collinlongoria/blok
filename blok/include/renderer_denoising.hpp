/*
* File: renderer_temporal_reprojection.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/

#ifndef RENDERER_TEMPORAL_REPROJECTION_HPP
#define RENDERER_TEMPORAL_REPROJECTION_HPP
#include <array>
#include "resources.hpp"
namespace blok {

class Renderer;

struct DenoiserPipeline {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // temporal accumulation pass
    vk::DescriptorSetLayout temporalSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> temporalSets;
    vk::PipelineLayout temporalPipelineLayout;
    vk::Pipeline temporalPipeline;

    // variance estimation pass
    vk::DescriptorSetLayout varianceSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> varianceSets;
    vk::PipelineLayout variancePipelineLayout;
    vk::Pipeline variancePipeline;

    // Atrous wavelet filter pass
    vk::DescriptorSetLayout atrousSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> atrousSets;
    vk::PipelineLayout atrousPipelineLayout;
    vk::Pipeline atrousPipeline;

    vk::Sampler linearSampler;
    vk::Sampler nearestSampler;
};

class Denoiser {
public:
    Renderer* renderer;

    GBuffer gbuffer;
    DenoiserPipeline pipeline;

    glm::mat4 prevView{1.0f};
    glm::mat4 prevProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    glm::vec3 prevCamPos{1.0f};
    bool hasPreviousFrame = false;

    // temporal settings
    // TODO imgui panel to edit these
    struct Settings {
        // Temporal accumulation
        float temporalAlpha = 0.12f;
        float momentAlpha = 0.4f;
        float varianceClipGamma = 1.0f;

        // Geometry rejection thresholds
        float depthThreshold = 0.01f;
        float normalThreshold = 0.98f;

        // Atrous filter parameters
        float phiColor = 3.0f;
        float phiNormal = 32.0f;
        float phiDepth = 0.5f;
        int atrousIterations = 3;

        // Variance estimation
        float varianceBoost = 2.0f;
        int minHistoryLength = 4;
    } settings;

public:
    explicit Denoiser(Renderer* r);

    void init(uint32_t width, uint32_t height);
    void cleanup();

    // called on swapchain recreate specifically
    void resize(uint32_t width, uint32_t height);

    void updatePreviousFrameData(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos);

    void fillFrameUBO(FrameUBO& ubo, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos, float deltaTime, int depth, uint32_t frameCount, uint32_t screenWidth, uint32_t screenHeight, int atrousIteration = 0);

    void denoise(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex);

    void swapHistoryBuffers(); // call after dispatch

    Image& getOutputImage();

private:
    void createGBuffer(uint32_t width, uint32_t height);
    void destroyGBuffer();
    void createSamplers();

    void createTemporalPipeline();
    void createVariancePipeline();
    void createAtrousPipeline();

    void createDescriptorSetLayouts();
    void allocateDescriptorSets();
    void updateDescriptorSets(uint32_t frameIndex);

    void dispatchTemporalAccumulation(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex);
    void dispatchVarianceEstimation(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex);
    void dispatchAtrousFilter(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex, int iteration);

    friend class Renderer;
};

}

#endif //RENDERER_TEMPORAL_REPROJECTION_HPP