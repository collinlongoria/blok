/*
* File: renderer_postprocess.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/8/2025
*/

#ifndef RENDERER_POSTPROCESS_HPP
#define RENDERER_POSTPROCESS_HPP

#include <array>
#include "resources.hpp"

namespace blok {

class Renderer;

struct PostProcessPipeline {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // TAA pass
    vk::DescriptorSetLayout taaSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> taaSets;
    vk::PipelineLayout taaPipelineLayout;
    vk::Pipeline taaPipeline;

    // Tone mapping pass
    vk::DescriptorSetLayout tonemapSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> tonemapSets;
    vk::PipelineLayout tonemapPipelineLayout;
    vk::Pipeline tonemapPipeline;

    // Sharpening pass
    vk::DescriptorSetLayout sharpenSetLayout;
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> sharpenSets;
    vk::PipelineLayout sharpenPipelineLayout;
    vk::Pipeline sharpenPipeline;

    vk::Sampler linearSampler;
    vk::Sampler nearestSampler;
};

struct PostProcessBuffers {
    // TAA history buffers
    Image taaHistory[2]; // TODO pretty sure these are the same as the ones in the G buffer?

    // Intermediate buffers
    Image taaOutput;
    Image tonemapOutput;
    Image sharpenOutput;

    uint32_t historyIndex = 0;

    Image& currentHistory() { return taaHistory[historyIndex]; }
    Image& previousHistory() { return taaHistory[1 - historyIndex]; }

    void swapHistory() { historyIndex = 1 - historyIndex; }
};

struct TAAPushConstants {
    float jitterX;
    float jitterY;
    float feedbackMin;
    float feedbackMax;
};

struct TonemapPushConstants {
    float exposure;
    float saturationBoost;
    int tonemapOperator;
    float whitePoint;
};

struct SharpenPushConstants {
    float sharpenStrength; // 0.0 - 1.0
    float padding[3];
};

enum class TonemapOperator : int {
    Neutral = 0,
    KhronosPBRNeutral = 1,
};

class PostProcess {
public:
    Renderer* renderer;

    PostProcessPipeline pipeline;
    PostProcessBuffers buffers;

    // TAA jitter sequence
    static constexpr int JITTER_SEQUENCE_LENGTH = 16;
    glm::vec2 jitterSequence[JITTER_SEQUENCE_LENGTH];
    uint32_t jitterIndex = 0;

    // Prev frame data for TAA
    glm::mat4 prevView{1.0f};
    glm::mat4 prevProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    bool hasPreviousFrame = false;

    struct Settings {
        // TAA settings
        bool enableTAA = true;
        float feedbackMin = 0.93f;
        float feedbackMax = 0.98f;
        float velocityRejectionScale = 1.0f;

        // Tonemap settings
        bool enableTonemapping = true;
        float exposure = 1.0f;
        float saturationBoost = 1.15f;
        TonemapOperator tonemapOperator = TonemapOperator::KhronosPBRNeutral;
        float whitePoint = 0.0f;

        // Sharpening settings
        bool enableSharpening = true;
        float sharpenStrength = 0.5f;
    } settings;

public:
    explicit PostProcess(Renderer* r);

    void init(uint32_t width, uint32_t height);
    void cleanup();

    void resize(uint32_t width, uint32_t height);

    glm::vec2 getJitterOffset() const;

    glm::vec2 getJitterClipSpace(uint32_t width, uint32_t height) const;

    void advanceJitter();

    void updatePreviousFrameData(const glm::mat4& view, const glm::mat4& proj);

    glm::mat4 getJitteredProjection(const glm::mat4& proj, uint32_t width, uint32_t height) const;

    void process(vk::CommandBuffer cmd, Image& inputColor, uint32_t width, uint32_t height, uint32_t frameIndex);

    Image& getOutputImage();

    void swapHistoryBuffers();

private:
    void createBuffers(uint32_t width, uint32_t height);
    void destroyBuffers();
    void createSamplers();

    void initJitterSequence();
    float halton(int index, int base);

    void createTAAPipeline();
    void createTonemapPipeline();
    void createSharpenPipeline();

    void createDescriptorSetLayouts();
    void allocateDescriptorSets();
    void updateDescriptorSets(uint32_t frameIndex, Image& inputColor);

    void dispatchTAA(vk::CommandBuffer cmd, Image& inputColor, uint32_t width, uint32_t height, uint32_t frameIndex);
    void dispatchTonemap(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex);
    void dispatchSharpen(vk::CommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameIndex);

    friend class Renderer;
};

}

#endif //RENDERER_POSTPROCESS_HPP