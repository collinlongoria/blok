/*
* File: Cuda_Tracer
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: Raytracing core
*/
#ifndef RENDERER_CUDA_HPP
#define RENDERER_CUDA_HPP

#include "renderer.hpp"
#include <cstdint>
#include <memory>

// forward decl so non-CUDA TUs don't need CUDA headers
struct float4;

namespace blok {

class Window;
class RendererGL;

class CudaTracer : public Renderer {
public:
    CudaTracer(unsigned int width, unsigned int height);
    ~CudaTracer() { shutdown(); }

    void init() override;
    void drawFrame(Camera& cam, const Scene& scene) override;
    void shutdown() override;
    void beginFrame() override;
    void endFrame() override;
    void resize(unsigned int w, unsigned int h);
    unsigned int getGLTex() const { return m_glTex; };
    void resetAccum();

    struct CamSig { float pos[3], fwd[3], right[3], up[3], fov; };

private:
    void cleanup();

    unsigned int m_width = 0;
    unsigned int m_height = 0;

    unsigned int m_pbo = 0;
    unsigned int m_glTex = 0;

    struct cudaGraphicsResource* m_cudaPBO = nullptr;

    float4*   m_dAccum = nullptr; // device accumulation buffer
    uint32_t  m_frameIndex = 0;

    CamSig m_prevCam{};
    bool   m_hasPrevCam = false;

    std::shared_ptr<Window> m_window;
};

} // namespace blok
#endif
