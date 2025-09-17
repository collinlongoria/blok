/*
* File: app
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/12/2025
*/

#include "app.hpp"
#include "window.hpp"

#include "Renderer_GL.hpp"

#include "Cuda_Tracer.hpp"

#include <stdexcept>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

using namespace blok;

App::App(RenderBackend backend)
    : m_backend(backend) {}

App::~App() {
    shutdown();
}

void App::run() {
    init();

    while (m_window && !m_window->shouldClose()) {
        update();
    }

    shutdown();
}

void App::init() {
    m_window = std::make_shared<Window>(800, 600, "blok");

    GLFWwindow* gw = m_window->getGLFWwindow();
    if (!gw) throw std::runtime_error("GLFW window creation failed");
    glfwMakeContextCurrent(gw);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to load GL with GLAD");
    }

    m_rendererGL = std::make_unique<RendererGL>(m_window);
    m_rendererGL->init();

    switch (m_backend) {
        case RenderBackend::OpenGL:
            break;

        case RenderBackend::CUDA:
            m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
            m_cudaTracer->init();
            break;

        case RenderBackend::WebGPU:
            // wire WGPU here
            break;
    }
}

void App::update() {
    unsigned int tex = 0;

    switch (m_backend) {
        case RenderBackend::CUDA:
            m_cudaTracer->render();
            tex = m_cudaTracer->getGLTex();
            break;

        case RenderBackend::OpenGL:
            tex = 0;
            break;

        case RenderBackend::WebGPU:
            tex = 0;
            break;
    }

    static_cast<RendererGL*>(m_rendererGL.get())->setTexture(tex, m_window->getWidth(), m_window->getHeight());
    m_rendererGL->drawFrame();

    Window::pollEvents();
    glfwSwapBuffers(m_window->getGLFWwindow());
}

void App::shutdown() {
    if (m_rendererGL) { m_rendererGL->shutdown(); m_rendererGL.reset(); }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }
}