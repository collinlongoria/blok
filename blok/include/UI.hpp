/*
* File: app
* Project: blok
* Author: Julia Moraes
* Created on: 9/12/2025
*/
#ifndef UI_HPP
#define UI_HPP



#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

void addWindow();



namespace blok {

    class Camera;

    class UI {
    public:
        enum MouseBehaviour
        {
            DEFAULT = 0,
            CAMERA_HOVER,
            CAMERA_CONTROL
        };

        UI(GLFWwindow* window);

        ~UI();

        /*Handles Camera movement through mouse controls*/
        void handleCameraControls(Camera* camera);
        
    private:

        struct MouseData
        {
            float lastX = 0.0f;
            float lastY = 0.0f;
            bool firstMouse = true;
        } m_mouseData;
        
         MouseBehaviour m_mouseSetting;
         GLFWwindow* m_window;
         Camera* m_camera;

         

         static void mouseCameraCallback(GLFWwindow* window, double xpos, double ypos);
         void swapMouseBehaviour(MouseBehaviour behaviour);
    };

} // namespace blok

#endif