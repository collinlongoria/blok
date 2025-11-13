/*
* File: app
* Project: blok
* Author: Julia Moraes
* Created on: 9/12/2025
*/
#ifndef UI_HPP
#define UI_HPP

#include "window.hpp"
#include <memory> 
#include <string>

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

        UI(const std::shared_ptr<Window>& window);

        ~UI();

        /*Handles Camera movement through mouse controls*/
        void handleCameraControls(Camera* camera);

        //void renderToNewWindow(unsigned int texture, std::string windowName = "");
        void renderToWindow(unsigned int texture);

        /*Displays App Information as a Child Window (WIP)*/
        void displayData(float dt);

        
        void beginWindow(std::string windowName = "New Window");
        /*Must be called after beginWindow*/
        void endWindow();
        
    private:

        struct MouseData
        {
            float lastX = 0.0f;
            float lastY = 0.0f;
            bool firstMouse = true;
        } m_mouseData;
        
         /*Controls*/
         MouseBehaviour m_mouseSetting;
         const std::shared_ptr<Window>& m_window;
         Camera* m_camera;

         /*Data*/
         double averageFps;
         unsigned frameCount;

         static void mouseCameraCallback(GLFWwindow* window, double xpos, double ypos);
         void swapMouseBehaviour(MouseBehaviour behaviour);
    };

} // namespace blok

#endif