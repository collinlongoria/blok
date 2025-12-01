/*
* File: Renderer_GL
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: generates quad
*/
#ifndef RENDERER_GL_HPP
#define RENDERER_GL_HPP
#include <memory>
#include <cstdint>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>


#include "renderer.hpp"


namespace blok {

class Window;
class Camera;
class UI;
struct Scene;

class RendererGL {
public:
    explicit RendererGL(std::shared_ptr<Window> window);
    ~RendererGL();

    void init();
    void drawFrame(const Camera& cam, const Scene& scene);
    void shutdown();

    // external (e.g. CUDA) provides texture to display
    void setTexture(unsigned int tex, unsigned int w, unsigned int h);

    void setUI(UI* ui);

    void beginFrame();
    void endFrame();

private:
    std::shared_ptr<Window> m_window;
    UI* m_ui;

    unsigned int m_tex = 0;
    int m_texW = 0, m_texH = 0;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_prog = 0;

    void createFullScreenQuad();
    void destroyFullScreenQuad();

    bool active = false;
};

} // namespace blok
#endif