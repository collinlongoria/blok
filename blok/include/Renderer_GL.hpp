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

namespace blok {

class Window;
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void init() = 0;
    virtual void drawFrame() = 0;
    virtual void shutdown() = 0;
};

class RendererGL : public IRenderer {
public:
    explicit RendererGL(std::shared_ptr<Window> window);
    ~RendererGL() override;

    void init() override;
    void drawFrame() override;
    void shutdown() override;

    void setTexture(unsigned int tex, unsigned int w, unsigned int h);

private:
    std::shared_ptr<Window> m_window;

    unsigned int m_tex = 0;
    int m_texW = 0, m_texH = 0;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_prog = 0;

    void createFullScreenQuad();
    void destroyFullScreenQuad();
};

} // namespace blok
#endif