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
#include "scene.hpp"

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

            { // voxel test
    float centerX = 64.0f;
    float centerY = 64.0f;
    float centerZ = 64.0f;

    float headRadius = 50.0f;
    float eyeRadius = 8.0f;
    float pupilRadius = 4.0f;
    float noseRadius = 5.0f;

    // Draw the head - yellow sphere
    g_mgr.setBrushColor(255, 220, 50);
    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - centerX;
                float dy = y - centerY;
                float dz = z - centerZ;
                float dist = dx*dx + dy*dy + dz*dz;

                // Hollow sphere (shell only for performance)
                if (dist <= headRadius*headRadius && dist >= (headRadius-3)*(headRadius-3)) {
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }

    // Left eye socket - white sphere
    float leftEyeX = centerX - 18.0f;
    float leftEyeY = centerY + 15.0f;
    float leftEyeZ = centerZ + 38.0f;

    g_mgr.setBrushColor(255, 255, 255);
    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - leftEyeX;
                float dy = y - leftEyeY;
                float dz = z - leftEyeZ;
                float dist = dx*dx + dy*dy + dz*dz;

                if (dist <= eyeRadius*eyeRadius) {
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }

    // Left pupil - black
    g_mgr.setBrushColor(20, 20, 20);
    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - leftEyeX;
                float dy = y - leftEyeY;
                float dz = z - (leftEyeZ + 5.0f);
                float dist = dx*dx + dy*dy + dz*dz;

                if (dist <= pupilRadius*pupilRadius) {
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }

    // Right eye socket - white sphere
    float rightEyeX = centerX + 18.0f;
    float rightEyeY = centerY + 15.0f;
    float rightEyeZ = centerZ + 38.0f;

    g_mgr.setBrushColor(255, 255, 255);
    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - rightEyeX;
                float dy = y - rightEyeY;
                float dz = z - rightEyeZ;
                float dist = dx*dx + dy*dy + dz*dz;

                if (dist <= eyeRadius*eyeRadius) {
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }

    // Right pupil - black
    g_mgr.setBrushColor(20, 20, 20);
    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - rightEyeX;
                float dy = y - rightEyeY;
                float dz = z - (rightEyeZ + 5.0f);
                float dist = dx*dx + dy*dy + dz*dz;

                if (dist <= pupilRadius*pupilRadius) {
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }

    // Nose - orange small sphere
    g_mgr.setBrushColor(255, 150, 50);
    float noseX = centerX;
    float noseY = centerY;
    float noseZ = centerZ + 48.0f;

    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - noseX;
                float dy = y - noseY;
                float dz = z - noseZ;
                float dist = dx*dx + dy*dy + dz*dz;

                if (dist <= noseRadius*noseRadius) {
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }

    // Smile - red torus/arc carved into the front of the face
    g_mgr.setBrushColor(220, 50, 50);
    float smileY = centerY - 18.0f;
    float smileRadius = 22.0f;
    float smileThickness = 3.5f;

    for (int z = 0; z < 128; z++) {
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                float dx = x - centerX;
                float dy = y - smileY;
                float dz = z - centerZ;

                // Only front half of face
                if (dz < 35.0f) continue;

                // Arc in XY plane, only bottom half (smile curves down)
                float distXY = std::sqrt(dx*dx + dy*dy);
                float distFromArc = std::abs(distXY - smileRadius);

                // Check if on the smile arc and within face bounds
                float distFromCenter = dx*dx + dy*dy + dz*dz;
                if (distFromCenter <= (headRadius+2)*(headRadius+2) &&
                    distFromArc <= smileThickness &&
                    dy < 5.0f &&  // Only lower part
                    std::abs(dx) < smileRadius - 2.0f) {  // Cut off edges
                    g_mgr.setVoxel(glm::vec3(x, y, z));
                }
            }
        }
    }
            g_camera.position = glm::vec3(64, 64, 200);
}
            // Prepare GPU world SVO
            WorldSvoGpu gpuWorld;
            rebuildDirtyChunks(g_mgr, 16);
            packChunksToGpuSvo(g_mgr, gpuWorld);

            // Upload world to Renderer
            m_renderer->addWorld(gpuWorld);
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

            m_renderer->render(g_camera, dt);
        }
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