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

struct TemporalPipeline {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    vk::DescriptorSetLayout descriptorSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;

    vk::Sampler historySampler;
};

class TemporalReprojection {
public:
    Renderer* renderer;

    GBuffer gbuffer;
    TemporalPipeline pipeline;

    glm::mat4 prevView{};
    glm::mat4 prevProj{};
    glm::mat4 prevViewProj{};
    glm::vec3 prevCamPos{};
    bool hasPreviousFrame = false;

    // temporal settings
    // TODO imgui panel to edit these
    struct Settings {
        float temporalAlpha = 0.1f;
        float momentAlpha = 0.2f;
        float varianceClipGamma = 1.5f;
        float depthThreshold = 0.1f;
        float normalThreshold = 0.9f;
    } settings;

public:
    explicit TemporalReprojection(Renderer* r);

    void init(uint32_t width, uint32_t height);
    void cleanup();

    // called on swapchain recreate specifically
    void resize(uint32_t width, uint32_t height);

    void createDescriptorSetLayout();

    void allocateDescriptorSet();
    void updateDescriptorSet(uint32_t frameIndex);

    void createPipeline();

    void updatePreviousFrameData(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos);

    void fillFrameUBO(FrameUBO& ubo, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos, float deltaTime, uint32_t frameCount, uint32_t screenWidth, uint32_t screenHeight);

    void dispatch(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex);

    void swapHistoryBuffers(); // call after dispatch

    Image& getOutputImage() { return gbuffer.currentHistory(); }

private:
    void createGBuffer(uint32_t width, uint32_t height);
    void destroyGBuffer();
    void createHistoryBuffers(uint32_t width, uint32_t height);
    void destroyHistoryBuffers();
    void createSampler();
};

}

#endif //RENDERER_TEMPORAL_REPROJECTION_HPP