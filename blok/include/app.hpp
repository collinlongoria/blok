/*
* File: app.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*/
#ifndef BLOK_APP_HPP
#define BLOK_APP_HPP

#include <memory>
#include "backend.hpp"

namespace blok {
struct WorldSvoGpu;

class Window;
class Renderer;
class RendererGL;
class CudaTracer;

class App {
public:
    explicit App(GraphicsApi backend);
    ~App();

    void run();

private:
    void init();
    void update();
    void shutdown();

    GraphicsApi m_backend;

    std::shared_ptr<Window>  m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<RendererGL> m_rendererGL;
    std::unique_ptr<CudaTracer> m_cudaTracer;

    std::unique_ptr<WorldSvoGpu> m_gpuWorld;
};

} // namespace blok
#endif