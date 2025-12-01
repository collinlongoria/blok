/*
* File: app
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/12/2025
*/
#include "app.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>

#include "window.hpp"
#include "ui.hpp"

#include "renderer.hpp"
#include "Renderer_GL.hpp"
#include "Cuda_Tracer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "imgui.h"

#include "camera.hpp"
#include "chunk_manager.hpp"
#include "scene.hpp"

#define VKR reinterpret_cast<VulkanRenderer*>(m_renderer.get())

using namespace blok;
static Camera g_camera;
static Scene  g_scene;
static ChunkManager g_mgr(128, 1.0f);
static float lastX = 400.0f;
static float lastY = 300.0f;
static bool firstMouse = true;

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
*/

namespace blok {

App::App(GraphicsApi backend)
    : m_backend(backend) {}

App::~App() {
    // none beef
}

void App::run() {
    init();

    update();

    shutdown();
}

void App::init() {
    m_window = std::make_shared<Window>(800, 600, "blok", m_backend);

    GLFWwindow* gw = m_window->getGLFWwindow();
    if (m_backend == GraphicsApi::OpenGL) {
        glfwMakeContextCurrent(gw);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            throw std::runtime_error("Failed to load GL with GLAD");
        }
        m_renderer = std::make_unique<RendererGL>(m_window);
        m_renderer->init();
        glfwSetCursorPosCallback(gw, mouse_callback);
        glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    switch (m_backend) {
    case GraphicsApi::OpenGL:
        m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
        m_cudaTracer->init();
        break;
    case GraphicsApi::Vulkan: {
        m_renderer = std::make_unique<VulkanRenderer>(m_window.get());
        m_renderer->init();

        // resize callback
        glfwSetFramebufferSizeCallback(m_window->getGLFWwindow(), vulkanFramebufferCallback);

        Object obj{};
        // create object from mesh
        VKR->initObjectFromMesh(obj, "mesh_flat", "assets/models/teapot/teapot.obj");

        // configure transform / material as needed
        obj.model.translation = {0.0f, 0.0f, -100.0f};
        obj.model.scale = {0.1f, 0.1f, 0.1f};
        obj.pipelineName = "mesh_flat";
        VKR->buildMaterialSetForObject(obj, "assets/models/teapot/default.png");

        gScene.push_back(obj);

        VKR->setRenderList(&gScene);

        //glfwSetCursorPosCallback(gw, mouse_callback);
        //glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
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
        case GraphicsApi::OpenGL:
            // ImGui + present path (one swap inside endFrame)
            m_cudaTracer->drawFrame(g_camera, g_scene);
            m_renderer->beginFrame();
            reinterpret_cast<RendererGL*>(m_renderer.get())->setTexture(m_cudaTracer->getGLTex(), m_window->getWidth(), m_window->getHeight());
            m_renderer->drawFrame(g_camera, g_scene);
            m_renderer->endFrame();
            break;

        case GraphicsApi::Vulkan:
            m_renderer->beginFrame();
            ImGui::ShowDemoWindow();
            m_renderer->drawFrame(g_camera, g_scene);
            m_renderer->endFrame();
            break;
    }
    }
}

void App::shutdown() {
    if (m_renderer) { /*m_renderer->shutdown();*/ m_renderer.reset(); }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }
    if (g_ui != nullptr) { delete g_ui; }

    if (m_backend == GraphicsApi::Vulkan) {
        m_window.reset();
    }
}

} // namespace blok