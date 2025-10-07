/*
* File: window
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/8/2025
*/

/*
* File: window.cpp
* Project: blok
* Description: GLFW wrapper window implementation
*/

#include "window.hpp"

#include <iostream>
#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

using namespace blok;

Window::Window(uint32_t width, uint32_t height, const std::string& name, GraphicsApi backend)
    : m_width(width), m_height(height), m_name(name), m_window(nullptr), m_backend(backend)
{
    static bool s_initialized = false;
    if (!s_initialized) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
        s_initialized = true;
    }

    if (backend == GraphicsApi::OpenGL || backend == GraphicsApi::CUDA) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }
    else if (backend == GraphicsApi::Vulkan) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(m_width),
                                static_cast<int>(m_height),
                                m_name.c_str(),
                                nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int fbw, int fbh) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self) self->onResize(fbw, fbh);
    });
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
}

void Window::pollEvents() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

void Window::onResize(int width, int height) {
    m_width  = static_cast<uint32_t>(width);
    m_height = static_cast<uint32_t>(height);
}