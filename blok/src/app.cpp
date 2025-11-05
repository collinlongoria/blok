/*
* File: app
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/12/2025
*/

#include "app.hpp"
#include "window.hpp"

#include "Renderer_GL.hpp"
#include "vulkan_renderer.hpp"

#include "Cuda_Tracer.hpp"

#include <stdexcept>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

/* These includes are here so I can test build */
#include <chrono>

#define VKR reinterpret_cast<VulkanRenderer*>(m_renderer.get())

using namespace blok;
static Camera g_camera;
static Scene  g_scene;
static float lastX = 400.0f;
static float lastY = 300.0f;
static bool firstMouse = true;
static std::vector<Object> gScene;

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

App::App(GraphicsApi backend)
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

        struct Vtx { float x,y,z,r,g,b; };

        const Vtx cubeVerts[] = {
            {-0.5f,-0.5f,-0.5f, 1,0,0},
            { 0.5f,-0.5f,-0.5f, 0,1,0},
            { 0.5f, 0.5f,-0.5f, 0,0,1},
            {-0.5f, 0.5f,-0.5f, 1,1,0},
            {-0.5f,-0.5f, 0.5f, 1,0,1},
            { 0.5f,-0.5f, 0.5f, 0,1,1},
            { 0.5f, 0.5f, 0.5f, 1,1,1},
            {-0.5f, 0.5f, 0.5f, 0,0,0},
        };

        const uint32_t cubeIdx[] = {
            // back
            0,3,2,  2,1,0,
            // front
            4,5,6,  6,7,4,
            // left
            0,4,7,  7,3,0,
            // right
            1,2,6,  6,5,1,
            // bottom
            0,1,5,  5,4,0,
            // top
            3,7,6,  6,2,3
        };

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

        glfwSetCursorPosCallback(gw, mouse_callback);
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
            m_renderer->drawFrame(g_camera, g_scene);
            m_renderer->endFrame();
            break;
    }
}

void App::shutdown() {
    if (m_renderer) { /*m_renderer->shutdown();*/ m_renderer.reset(); }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }

    if (m_backend == GraphicsApi::Vulkan) {
        m_window.reset();
    }
}

} // namespace blok