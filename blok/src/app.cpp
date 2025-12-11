/*
* File: app.cpp
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/12/2025
*/
#include "app.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>

#include "window.hpp"

#include "renderer.hpp"
#include "renderer_gl.hpp"
#include "cuda_tracer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "imgui.h"

#include "ui.hpp"
#include "camera.hpp"
#include "chunk_manager.hpp"
#include "imgui_impl_glfw.h"
#include "scene.hpp"
#include "vox_loader.hpp"

#define VKR reinterpret_cast<VulkanRenderer*>(m_renderer.get())

using namespace blok;
static Camera g_camera;
static Scene  g_scene;
static UI* g_ui;
static ChunkManager g_mgr(128, 1.0f);
static float lastX = 400.0f;
static float lastY = 300.0f;
static bool firstMouse = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
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

App::~App() {}

void App::run() {
    init();

    update();

    shutdown();
}

void App::init() {

    switch (m_backend) {
        case GraphicsApi::OpenGL: {
            m_window = std::make_shared<Window>(800, 600, "blok", m_backend);
            GLFWwindow* gw = m_window->getGLFWwindow();

            g_ui = new UI(m_window);

            glfwMakeContextCurrent(gw);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                throw std::runtime_error("Failed to load GL with GLAD");
            }
            m_rendererGL = std::make_unique<RendererGL>(m_window);
            m_rendererGL->init();
            reinterpret_cast<RendererGL*>(m_rendererGL.get())->setUI(g_ui);

            m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
            m_cudaTracer->init();
            break;
        }
        case GraphicsApi::Vulkan: {
            m_renderer = std::make_unique<Renderer>(1280, 720);
            auto gw = m_renderer->getWindow();
            glfwSetCursorPosCallback(gw, mouse_callback);
            glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            blok::MaterialLibrary& matLib = m_renderer->getMaterialLibrary();
            g_mgr.setMaterialLibrary(&matLib);

            VoxFile vox;
            std::string err;
            bool success = blok::loadAndImportVox(
                "assets/models/menger.vox",
                g_mgr,
                &matLib,
                glm::vec3(0, 0, 0),
                0,
                &err
            );

            if (!success) {
                std::cerr << "Failed to load VOX: " << err << "\n";
            }

            // Prepare GPU world SVO
            m_gpuWorld = std::make_unique<WorldSvoGpu>();
            rebuildDirtyChunks(g_mgr, 16);
            packChunksToGpuSvo(g_mgr, *m_gpuWorld);

            // Upload world to Renderer
            m_renderer->addWorld(*m_gpuWorld);
        }
            break;
    }
}

void App::update() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();

    switch (m_backend) {
    case GraphicsApi::OpenGL: {
        while (m_window && !m_window->shouldClose()) {
            auto now = clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            Window::pollEvents();

            GLFWwindow* win = m_window->getGLFWwindow();
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camera.processKeyboard('W', dt);
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camera.processKeyboard('S', dt);
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) g_camera.processKeyboard('A', dt);
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_camera.processKeyboard('D', dt);

            if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, true);

            // ImGui + present path (one swap inside endFrame)
            m_cudaTracer->drawFrame(g_camera, g_scene);
            m_rendererGL->beginFrame();
            reinterpret_cast<RendererGL*>(m_rendererGL.get())->setTexture(m_cudaTracer->getGLTex(), m_window->getWidth(), m_window->getHeight());
            m_rendererGL->drawFrame(g_camera, g_scene);

            addWindow();
            g_ui->displayData(dt);

            m_rendererGL->endFrame();
        }
        break;
    }

    case GraphicsApi::Vulkan: {
        while (!glfwWindowShouldClose(m_renderer->getWindow())) {
            auto now = clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            glfwPollEvents();

            GLFWwindow* win = m_renderer->getWindow();
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camera.processKeyboard('W', dt);
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camera.processKeyboard('S', dt);
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) g_camera.processKeyboard('A', dt);
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_camera.processKeyboard('D', dt);
            if (glfwGetKey(win, GLFW_KEY_X) == GLFW_PRESS) g_camera.processKeyboard('X', dt);
            if (glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS) g_camera.processKeyboard('Z', dt);

            if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, true);

            float fps = 1.0f / dt;
            float ms = dt * 1000.0f;
            m_renderer->updatePerformanceData((int)fps, ms);

            m_renderer->render(g_camera, dt);
        }
        break;
    }
    }
}

void App::shutdown() {
    if (m_renderer) {
        m_renderer.reset();
        m_gpuWorld.reset();
    }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }
    if (g_ui != nullptr) { delete g_ui; }
    if (m_backend == GraphicsApi::Vulkan) {
        m_window.reset();
    }
}

} // namespace blok