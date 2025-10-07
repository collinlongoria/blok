/*
* File: window
* Project: blok
* Author: Collin Longoria
* Created on: 9/8/2025
*
* Description: GLFW wrapper window class
*/

#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <string>
#include <cstdint>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "backend.hpp"

namespace blok {

class Window {
public:
    Window(uint32_t width, uint32_t height, const std::string& name, GraphicsApi backend);
    ~Window();

    static void pollEvents();

    [[nodiscard]] bool shouldClose() const;

    [[nodiscard]] GLFWwindow* getGLFWwindow() const { return m_window; }

    [[nodiscard]] uint32_t getWidth() const { return m_width; }
    [[nodiscard]] uint32_t getHeight() const { return m_height; }
    [[nodiscard]] const std::string& getName() const { return m_name; }
    [[nodiscard]] GraphicsApi getApi() const { return m_backend; }

    void setKeyCallback(const GLFWkeyfun callback) const {
        if (m_window) glfwSetKeyCallback(m_window, callback);
    }

private:
    void onResize(int width, int height);

    uint32_t    m_width = 0, m_height = 0;
    std::string m_name;
    GraphicsApi m_backend;
    GLFWwindow* m_window = nullptr;
};

} // namespace blok

#endif // WINDOW_HPP