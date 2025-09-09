/*
* File: window
* Project: blok
* Author: Collin Longoria
* Created on: 9/8/2025
*/

#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <string>

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

namespace blok {
    class Window {
    public:
        Window(uint32_t width, uint32_t height, const std::string& name);
        ~Window();

        // Poll and process GLFW events
        static void pollEvents();

        // Returns if window should close
        [[nodiscard]] bool shouldClose() const;

        // Provides access to raw GLFWwindow pointer
        [[nodiscard]] GLFWwindow* getGLFWwindow() const {
            return m_window;
        }

        // Getters for window properties
        [[nodiscard]] uint32_t getWidth() const { return m_width; }
        [[nodiscard]] uint32_t getHeight() const { return m_height; }
        [[nodiscard]] const std::string& getName() const { return m_name; }

        // Set GLFW input callback for the window
        // TODO: Ideally this needs to be re-written to not expose GLFW
        void setKeyCallback(const GLFWkeyfun callback) const {
            if(m_window) {
                glfwSetKeyCallback(m_window, callback);
            }
        }

    private:
        void onResize(int width, int height);

        uint32_t m_width, m_height;
        std::string m_name;
        GLFWwindow* m_window;
    };
}

#endif //WINDOW_HPP
