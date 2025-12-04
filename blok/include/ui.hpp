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
#include "math.hpp"

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

        UI(Window* window);

        ~UI();

        void update(float dt);

        /*Handles Camera movement through mouse controls*/
        void handleCameraControls(Camera* camera);

        //void renderToNewWindow(unsigned int texture, std::string windowName = "");
        void renderToWindow(unsigned int texture);

        /*Displays App Information as a Child Window (WIP)*/
        void displayData();

        
        void beginWindow(Vector2 position, std::string windowName = "New Window");
        void beginWindow(std::string windowName = "New Window");
        /*Must be called after beginWindow*/
        void endWindow();

        void createButton(std::string windowName = "New Button", void func() = nullptr);
        
    private:

        struct MouseData
        {
            float lastX = 0.0f;
            float lastY = 0.0f;
            bool firstMouse = true;
        } m_mouseData;
        
         /*Controls*/
         MouseBehaviour m_mouseSetting;
         Window* m_window;
         Camera* m_camera;

         /*Data*/
         double averageFps;
         unsigned frameCount;
         float dt;

         /*Arrangement*/
         Vector2 nextWindowPos;

         static void mouseCameraCallback(GLFWwindow* window, double xpos, double ypos);
         void swapMouseBehaviour(MouseBehaviour behaviour);
    };

} // namespace blok

#endif