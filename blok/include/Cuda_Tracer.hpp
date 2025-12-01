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
struct Scene;

class Window;
class RendererGL;

class CudaTracer {
public:
    CudaTracer(unsigned int width, unsigned int height);
    ~CudaTracer();

    void init();
    void drawFrame(const Camera& cam, const Scene& scene);
    void shutdown();
    void beginFrame();
    void endFrame();

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
