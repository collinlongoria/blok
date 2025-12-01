#ifndef RENDERER_RAYTRACING_HPP
#define RENDERER_RAYTRACING_HPP
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

    vk::DescriptorSetLayout rtSetLayout{};
    vk::DescriptorSet rtSet{};

    RayTracingPipeline rtPipeline{};

public:
    RayTracing(Renderer* r);

    void createDescriptorSetLayout();
    void allocateDescriptorSet();
    void updateDescriptorSet(const WorldSvoGpu&);

    void createPipeline();
    void createSBT();

    void dispatchRayTracing(vk::CommandBuffer cmd, uint32_t w, uint32_t h);
};

}

#endif //RENDERER_RAYTRACING_HPP