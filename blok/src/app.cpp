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

/* These includes are here so I can test build */
#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include "gpu_flags.hpp"
#include "gpu_handles.hpp"
#include "webgpu_device.hpp"

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
    m_window = std::make_shared<Window>(800, 600, "blok", m_backend);

    GLFWwindow* gw = m_window->getGLFWwindow();
    glfwMakeContextCurrent(gw);
    if (m_backend == RenderBackend::CUDA || m_backend == RenderBackend::OpenGL) {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            throw std::runtime_error("Failed to load GL with GLAD");
        }
        m_rendererGL = std::make_unique<RendererGL>(m_window);
        m_rendererGL->init();
    }

    switch (m_backend) {
        case RenderBackend::OpenGL:
            break;

        case RenderBackend::CUDA:
            m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
            m_cudaTracer->init();
            break;

        case RenderBackend::WebGPU:
        {
            DeviceInitInfo init{};
            init.width = static_cast<uint32_t>(m_window->getWidth());
            init.height = static_cast<uint32_t>(m_window->getHeight());
            init.backBufferFormat = Format::RGBA8_UNORM;
            init.presentMode = PresentMode::VSYNC;
            init.windowHandle = m_window.get();

            m_gpu = std::make_unique<WebGPUDevice>(init);
        }
            break;
    }
}

void App::update() {
    Window::pollEvents();

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

    if (m_backend == RenderBackend::OpenGL || m_backend == RenderBackend::CUDA) {
        dynamic_cast<RendererGL*>(m_rendererGL.get())->setTexture(tex, m_window->getWidth(), m_window->getHeight());
        m_rendererGL->drawFrame();
    }

    // TODO: Ideally this is happening in renderer interface
    glfwSwapBuffers(m_window->getGLFWwindow());
}

void App::shutdown() {
    if (m_rendererGL) { m_rendererGL->shutdown(); m_rendererGL.reset(); }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }
}