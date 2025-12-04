/*
* File: renderer_raytracing.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef RENDERER_RAYTRACING_HPP
#define RENDERER_RAYTRACING_HPP
#include <array>
#include "resources.hpp"
#include "vulkan_context.hpp"

namespace blok {
class Renderer;

struct RayTracingPipeline {
    vk::Pipeline pipeline{};
    vk::PipelineLayout layout{};

    Buffer rgenSBT;
    Buffer missSBT;
    Buffer hitSBT;

    vk::StridedDeviceAddressRegionKHR rgenRegion;
    vk::StridedDeviceAddressRegionKHR missRegion;
    vk::StridedDeviceAddressRegionKHR hitRegion;
    vk::StridedDeviceAddressRegionKHR callRegion;
};

class RayTracing {
public:
    Renderer* r;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    vk::DescriptorSetLayout rtSetLayout{};
    std::array<vk::DescriptorSet, MAX_FRAMES_IN_FLIGHT> rtSets{};

    RayTracingPipeline rtPipeline{};

public:
    explicit RayTracing(Renderer* r);

    void createDescriptorSetLayout();
    void allocateDescriptorSet();
    void updateDescriptorSet(const WorldSvoGpu&, uint32_t frameIndex);

    void createPipeline();
    void createSBT();

    void dispatchRayTracing(vk::CommandBuffer cmd, uint32_t w, uint32_t h, uint32_t frameIndex);
};

}

#endif //RENDERER_RAYTRACING_HPP