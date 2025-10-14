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
#include <chrono>

#include "webgpu_device.hpp"
#include "webgpu_renderer.hpp"

#include <windows.h>

using namespace blok;
static Camera g_camera;
static Scene  g_scene;
static float lastX = 400.0f;
static float lastY = 300.0f;
static bool firstMouse = true;

// ---------------- Input Callbacks ----------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float dx = (float)xpos - lastX;
    float dy = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    g_camera.processMouse(dx, dy);
}

namespace blok {

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
    if (m_backend == RenderBackend::CUDA || m_backend == RenderBackend::OpenGL) {
        glfwMakeContextCurrent(gw);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            throw std::runtime_error("Failed to load GL with GLAD");
        }
        m_rendererGL = std::make_unique<RendererGL>(m_window);
        m_rendererGL->init();
        glfwSetCursorPosCallback(gw, mouse_callback);
        glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    else if (m_backend == RenderBackend::WEBGPU_VULKAN || m_backend == RenderBackend::WEBGPU_D3D12) {
        m_rendererWebGPU = std::make_unique<RendererWebGPU>(m_window);
        glfwSetCursorPosCallback(gw, mouse_callback);
        //glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    switch (m_backend) {
        case RenderBackend::OpenGL:
            break;

        case RenderBackend::CUDA:
            m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
            m_cudaTracer->init();
            break;

        case RenderBackend::WEBGPU_D3D12:
        case RenderBackend::WEBGPU_VULKAN:
        {
           m_rendererWebGPU->init();
        }
            break;
    }
}

void App::update() {
    Window::pollEvents();

    static double lastFrame = glfwGetTime();
    double now = glfwGetTime();
    float dt = static_cast<float>(now - lastFrame);
    lastFrame = now;

    GLFWwindow* win = m_window->getGLFWwindow();
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camera.processKeyboard('W', dt);
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camera.processKeyboard('S', dt);
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) g_camera.processKeyboard('A', dt);
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_camera.processKeyboard('D', dt);


    switch (m_backend) {
        case RenderBackend::CUDA:
            // ImGui + present path (one swap inside endFrame)
            m_cudaTracer->drawFrame(g_camera, g_scene);
            m_rendererGL->beginFrame();
            m_rendererGL->setTexture(m_cudaTracer->getGLTex(),
                                    m_window->getWidth(),
                                    m_window->getHeight());
            m_rendererGL->drawFrame(g_camera, g_scene);
            m_rendererGL->endFrame();
            break;

        case RenderBackend::OpenGL:
            // ImGui + present path (one swap inside endFrame)
            m_rendererGL->beginFrame();
            m_rendererGL->drawFrame(g_camera, g_scene);
            m_rendererGL->endFrame();
            break;

        case RenderBackend::WEBGPU_D3D12:
        case RenderBackend::WEBGPU_VULKAN:
            m_rendererWebGPU->beginFrame();
            m_rendererWebGPU->drawFrame(g_camera, g_scene);
            m_rendererWebGPU->endFrame();
            break;
    }
}

void App::shutdown() {
    if (m_rendererGL) { m_rendererGL->shutdown(); m_rendererGL.reset(); }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }

    if (m_backend == RenderBackend::WEBGPU_D3D12 || m_backend == RenderBackend::WEBGPU_VULKAN) {
       m_rendererWebGPU->shutdown();
        m_window.reset();
    }
}

} // namespace blok