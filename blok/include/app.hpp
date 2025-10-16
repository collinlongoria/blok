/*
* File: app
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*
* Description: Main entry point for blok
*/
#ifndef BLOK_APP_HPP
#define BLOK_APP_HPP

#include <memory>
#include <string>
#include "backend.hpp"

namespace blok {

class Window;
class Renderer;
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
    std::unique_ptr<CudaTracer> m_cudaTracer;
};

} // namespace blok
#endif