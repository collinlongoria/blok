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

namespace blok {

class Window;
class RendererGL;

class CudaTracer : public Renderer {
public:
    CudaTracer(unsigned int width, unsigned int height);
    ~CudaTracer() override;

    void init() override;
    void drawFrame(Camera& cam, const Scene& scene) override;
    void shutdown() override;
    void beginFrame() override;
    void endFrame() override;

    unsigned int getGLTex() const { return m_glTex; }

private:
    void cleanup();

    unsigned int m_width = 0;
    unsigned int m_height = 0;

    unsigned int m_pbo = 0;   
    unsigned int m_glTex = 0; 

    struct cudaGraphicsResource* m_cudaPBO = nullptr;

    std::shared_ptr<Window> m_window;
};

} // namespace blok
#endif
