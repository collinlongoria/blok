/*
* File: window
* Project: blok
* Author: Collin Longoria
* Created on: 9/8/2025
*/

#include "window.hpp"

#include <iostream>
#include <ostream>
#include <stdexcept>

using namespace blok;

Window::Window(uint32_t width, uint32_t height, const std::string &name)
    : m_width(width), m_height(height), m_name(name), m_window(nullptr) {
    // If a window already initialized GLFW, don't do it again
    static bool initialized = false;
    if(!initialized) {
        if(!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
        }
        initialized = true;
    }

    // TODO: Implement Render APIs
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // for now, remove later

    GLFWwindow *window = glfwCreateWindow(static_cast<int>(m_width), static_cast<int>(m_height),
        m_name.c_str(), nullptr, nullptr);
    if(!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        initialized = false;
    }

    m_window = window;
    glfwSetWindowUserPointer(window, this);

    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow *window, int width, int height) {
       auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if(self) self->onResize(width, height);
    });
}

Window::~Window() {
    if(m_window) {
        glfwDestroyWindow(m_window);
    }

    // TODO: if multiple windows, add static ref count and only terminate on last window destroyed
    glfwTerminate();
}

void Window::pollEvents() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

void Window::onResize(int width, int height) {
    m_width = width;
    m_height = height;

    // TODO: When renderer API is added, send info to it here
}
