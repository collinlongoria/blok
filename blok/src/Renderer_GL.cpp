/*
* File: Renderer_GL
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
* Description: GL quad and shader setup
*/


#include "Renderer_GL.hpp"
#include "window.hpp"

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>

//imgui
#include <imgui.h>
#include "backends/imgui_impl_glfw.h"   
#include "backends/imgui_impl_opengl3.h"

using namespace blok;

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        throw std::runtime_error(std::string("Shader compile failed: ") + log);
    }
    return s;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, 1024, nullptr, log);
        throw std::runtime_error(std::string("Program link failed: ") + log);
    }
    return p;
}

RendererGL::RendererGL(std::shared_ptr<Window> window)
    : m_window(std::move(window)) {}

RendererGL::~RendererGL() { if (active) { shutdown(); } }

void RendererGL::setTexture(unsigned int tex, unsigned int w, unsigned int h) { 
    m_tex = tex; m_texW = static_cast<int>(w); m_texH = static_cast<int>(h);
}

void RendererGL::beginFrame()
{
    //imgui
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    glClear(GL_COLOR_BUFFER_BIT);
}

void RendererGL::endFrame()
{
    //imgui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window->getGLFWwindow());

    
}

void RendererGL::init() {
    // simple textured full-screen quad
    const char* vsSrc = R"(#version 330 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec2 aUV;
        out vec2 vUV;
        void main(){
            vUV = aUV;
            gl_Position = vec4(aPos, 0.0, 1.0);
        })";

    const char* fsSrc = R"(#version 330 core
        in vec2 vUV;
        out vec4 frag;
        uniform sampler2D uTex;
        void main(){
            frag = texture(uTex, vUV);
        })";

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    m_prog = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    createFullScreenQuad();


    //imgui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window->getGLFWwindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");



    glViewport(0, 0, m_window->getWidth(), m_window->getHeight());
    glClearColor(0.1f, 0.1f, 0.25f, 1.0f);

    active = true;
}

void RendererGL::createFullScreenQuad() {
    // positions + UVs
    float verts[] = {
        //  x,   y,   u,  v
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 1.f
    };
    unsigned int idx[] = { 0,1,2,  2,3,0 };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1); // aUV
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void RendererGL::drawFrame() {
    //glClear(GL_COLOR_BUFFER_BIT);

    if (m_tex != 0) {
        /*
        glUseProgram(m_prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        GLint loc = glGetUniformLocation(m_prog, "uTex");
        glUniform1i(loc, 0);

        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        */

        ImGui::Begin("UI Window");
        ImGui::Text("Where UI stuff get's rendered:");
        ImGui::Image((ImTextureID)(intptr_t)m_tex, ImVec2((float)m_texW, (float)m_texH));
        ImGui::End();
        
    }
}

void RendererGL::destroyFullScreenQuad() {
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
}

void RendererGL::shutdown() {

    //imgui
    ImGui_ImplOpenGL3_Shutdown(); 
    ImGui_ImplGlfw_Shutdown(); 
    ImGui::DestroyContext();

    destroyFullScreenQuad();
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0;}

    
    active = false;
}