// Renderer_WebGPU.hpp
#pragma once
#include <memory>
#include <vector>
#include "gpu_types.hpp"
#include "webgpu_device.hpp"
#include "backend.hpp"
#include "window.hpp"
#include "camera.hpp"
#include "scene.hpp"

namespace blok {

class RendererWebGPU {
public:
    explicit RendererWebGPU(std::shared_ptr<Window> window);
    ~RendererWebGPU();

    void init();
    void beginFrame();
    void drawFrame(const Camera& cam, const Scene& scene);
    void endFrame();
    void shutdown();

    // Optional: expose resizing if you wire it later
    void resize(uint32_t w, uint32_t h);

private:
    struct Uniforms {
        float t;        // time in seconds
        int   width;
        int   height;
        float pad;      // align to 16
    };

    std::shared_ptr<Window> m_window;
    std::unique_ptr<WebGPUDevice> m_gpu;

    // swapchain
    SwapchainHandle m_swap{};

    // compute target (storage+sampled)
    ImageHandle m_rayImage{};
    ImageViewHandle m_rayView{};
    SamplerHandle m_sampler{};

    // uniform buffer for compute (time/size)
    BufferHandle m_ubo{};

    // pipelines & shaders
    ShaderModuleHandle m_cs{};
    ComputePipelineHandle m_cpipe{};
    PipelineLayoutHandle m_cpl{}
    ;
    BindGroupLayoutHandle m_cbgl{};
    BindGroupHandle m_cbg{};

    ShaderModuleHandle m_vs{};
    ShaderModuleHandle m_fs{};
    GraphicsPipelineHandle m_gpipe{};
    PipelineLayoutHandle m_gpl{};
    BindGroupLayoutHandle m_gbgl{};
    BindGroupHandle m_gbg{};

    // cached dims
    uint32_t m_width = 0, m_height = 0;

    // helpers
    void createSwapchain();
    void createRayTarget();
    void createSamplers();
    void createComputePipeline();
    void createBlitPipeline();
};

} // namespace blok
