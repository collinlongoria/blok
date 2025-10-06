/*
* File: app
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/12/2025
*/

#include "app.hpp"
#include "window.hpp"

#include "Renderer_GL.hpp"

#include "Cuda_Tracer.hpp"

#include <stdexcept>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

/* These includes are here so I can test build */
#include <chrono>

#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include "gpu_flags.hpp"
#include "gpu_handles.hpp"
#include "webgpu_device.hpp"

#include <windows.h>

using namespace blok;
static Camera g_camera;
static Scene  g_scene;
static float lastX = 400.0f;
static float lastY = 300.0f;
static bool firstMouse = true;

// ---------------- Input Callbacks ----------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float dx = (float)xpos - lastX;
    float dy = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    g_camera.processMouse(dx, dy);
}


// WebGPU Test Helpers
struct alignas(16) Uniforms {
    float t; // time (seconds)
    uint32_t N; // vertex count
    uint32_t pad0;
    uint32_t pad1;
};

struct VertexP4C4 {
    float px, py, pz, pw;
    float r, g, b, a;
};

static std::vector<VertexP4C4> makeInitialVerts() {
    const float s = 0.25f;
    std::vector<VertexP4C4> v;
    auto tri = [&](float cx, float cy, float r, float g, float b) {
        v.push_back({cx + 0.0f, cy + s,   cx, cy,  r,g,b,1.0f});
        v.push_back({cx - s,    cy - s,   cx, cy,  r,g,b,1.0f});
        v.push_back({cx + s,    cy - s,   cx, cy,  r,g,b,1.0f});
    };
    tri(-0.6f, 0.0f, 1.0f, 0.3f, 0.3f);
    tri( 0.0f, 0.0f, 0.3f, 1.0f, 0.3f);
    tri( 0.6f, 0.0f, 0.3f, 0.3f, 1.0f);
    return v;
}
static std::vector<VertexP4C4> makeBackground(){
    std::vector<VertexP4C4> v;
    v.push_back({-1,1,0,0,1,0,0,1});
    v.push_back({1,1,0,0,0,1,0,1});
    v.push_back({-1,-1,0,0,0,0,1,1});
    v.push_back({-1,-1,0,0,0,0,1,1});
    v.push_back({1,1,0,0,0,1,0,0});
    v.push_back({1,-1,0,0,0,1,0,1});
    return v;
}

// Read a whole text file (WGSL) into bytes
static std::vector<uint8_t> readTextFile(const char* path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::perror(path); return out; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n);
    if (n > 0) std::fread(out.data(), 1, n, f);
    std::fclose(f);
    return out;
}
// WebGPU Test Helpers

namespace blok {

App::App(RenderBackend backend)
    : m_backend(backend) {}

App::~App() {
    shutdown();
}

void App::run() {
    init();

    while (m_window && !m_window->shouldClose()) {
        update();
    }

    shutdown();
}

void App::init() {


    m_window = std::make_shared<Window>(800, 600, "blok", m_backend);

    GLFWwindow* gw = m_window->getGLFWwindow();
    if (m_backend == RenderBackend::CUDA || m_backend == RenderBackend::OpenGL) {
        glfwMakeContextCurrent(gw);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            throw std::runtime_error("Failed to load GL with GLAD");
        }
        m_rendererGL = std::make_unique<RendererGL>(m_window);
        m_rendererGL->init();
        glfwSetCursorPosCallback(gw, mouse_callback);
        glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    switch (m_backend) {
        case RenderBackend::OpenGL:
            break;

        case RenderBackend::CUDA:
            m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
            m_cudaTracer->init();
            break;

        case RenderBackend::WEBGPU_D3D12:
        case RenderBackend::WEBGPU_VULKAN:
        {
            DeviceInitInfo init{};
            init.width = static_cast<uint32_t>(m_window->getWidth());
            init.height = static_cast<uint32_t>(m_window->getHeight());
            init.presentMode = PresentMode::MAILBOX;
            init.backend = m_backend;
            init.windowHandle = m_window.get();

            m_gpu = std::make_unique<WebGPUDevice>(init);

            SwapchainDescriptor sd{};
            sd.width = init.width;
            sd.height = init.height;
            sd.format = m_gpu->backbufferFormat();
            sd.presentMode = init.presentMode;
            auto swap = m_gpu->createSwapchain(sd);

            auto verts = makeInitialVerts();
            const uint32_t VERT_COUNT = static_cast<uint32_t>(verts.size());

            BufferDescriptor vbDesc{};
            vbDesc.size = sizeof(VertexP4C4) * verts.size();
            vbDesc.usage = BufferUsage::VERTEX | BufferUsage::STORAGE | BufferUsage::COPYDESTINATION;
            auto vbuf = m_gpu->createBuffer(vbDesc, nullptr);
            m_gpu->updateBuffer(vbuf, 0, vbDesc.size, verts.data());

            auto verts2 = makeBackground();
            const uint32_t VERT_COUNT2 = static_cast<uint32_t>(verts2.size());
            BufferDescriptor vbDesc2{};
            vbDesc2.size = sizeof(VertexP4C4) * verts2.size();
            vbDesc2.usage = BufferUsage::VERTEX | BufferUsage::COPYDESTINATION;
            auto vbuf2 = m_gpu->createBuffer(vbDesc2, nullptr);
            m_gpu->updateBuffer(vbuf2, 0, vbDesc2.size, verts2.data());

            BufferDescriptor ubDesc{};
            ubDesc.size = sizeof(Uniforms);
            ubDesc.usage = BufferUsage::UNIFORM | BufferUsage::COPYDESTINATION;
            auto ubuf = m_gpu->createBuffer(ubDesc, nullptr);

            BindGroupLayoutDescriptor bglDesc{};
            bglDesc.entries = {
                BindGroupLayoutEntry{
                    .binding = 0,
                    .type = BindingType::UNIFORMBUFFER,
                    .visibleStages = PipelineStage::COMPUTESHADER,
                },
                BindGroupLayoutEntry{
                    .binding = 1,
                    .type = BindingType::STORAGEBUFFER,
                    .visibleStages = PipelineStage::COMPUTESHADER
                },
            };
            auto bgl = m_gpu->createBindGroupLayout(bglDesc);

            IGPUDevice::BindGroupDescriptor bgDesc{};
            bgDesc.layout = bgl;
            bgDesc.entries = {
                IGPUDevice::BindGroupEntry{
                    .binding = 0,
                    .kind = IGPUDevice::BindGroupEntry::Kind::BUFFER,
                    .handle = static_cast<uint64_t>(ubuf),
                    .offset = 0,
                    .size = sizeof(Uniforms),
                },
                IGPUDevice::BindGroupEntry{
                    .binding = 1,
                    .kind = IGPUDevice::BindGroupEntry::Kind::BUFFER,
                    .handle = static_cast<uint64_t>(vbuf),
                    .offset = 0,
                    .size = sizeof(VertexP4C4) * verts.size(),
                }
            };
            auto bgroup = m_gpu->createBindGroup(bgDesc);

            PipelineLayoutDescriptor cplDesc{};
            cplDesc.setLayouts = { bgl };
            auto cpl = m_gpu->createPipelineLayout(cplDesc);

            PipelineLayoutDescriptor gplDesc{};
            gplDesc.setLayouts = {};
            auto gpl = m_gpu->createPipelineLayout(gplDesc);

            auto csSrc = readTextFile("assets/shaders/rotate.comp.wgsl");
            auto vsSrc = readTextFile("assets/shaders/tri.wgsl");
            auto fsSrc = vsSrc;

            ShaderModuleDescriptor csMod{};
            csMod.ir = ShaderIR::WGSL;
            csMod.bytes = std::span<const uint8_t>(csSrc.data(), csSrc.size());
            csMod.entryPoint = "cs_main";
            auto cs = m_gpu->createShaderModule(csMod);

            ShaderModuleDescriptor vsMod{};
            vsMod.ir = ShaderIR::WGSL;
            vsMod.bytes = std::span<const uint8_t>(vsSrc.data(), vsSrc.size());
            vsMod.entryPoint = "vs_main";
            auto vs = m_gpu->createShaderModule(vsMod);

            ShaderModuleDescriptor fsMod{};
            fsMod.ir = ShaderIR::WGSL;
            fsMod.bytes = std::span<const uint8_t>(fsSrc.data(), fsSrc.size());
            fsMod.entryPoint = "fs_main";
            auto fs = m_gpu->createShaderModule(fsMod);

            ComputePipelineDescriptor cpd{};
            cpd.cs = cs;
            cpd.pipelineLayout = cpl;
            auto cpipe = m_gpu->createComputePipeline(cpd);

            GraphicsPipelineDescriptor gpd{};
            gpd.vs = vs;
            gpd.fs = fs;
            gpd.pipelineLayout = gpl;
            gpd.primitiveTopology = PrimitiveTopology::TRIANGLELIST;
            gpd.frontFace = FrontFace::CCW;
            gpd.cull = CullMode::NONE;
            gpd.depth = { .depthTest = false, .depthWrite = false };
            gpd.depthFormat = Format::UNKNOWN;
            gpd.colorFormat = m_gpu->backbufferFormat();
            gpd.blend = { .enable = true };
            gpd.vertexInputs = {
                VertexAttributeDescriptor{
                    .location = 0,
                    .binding = 0,
                    .offset = 0,
                    .stride = sizeof(VertexP4C4),
                    .format = Format::RGBA32_FLOAT
                },
                VertexAttributeDescriptor{
                    .location = 1,
                    .binding = 0,
                    .offset = 16,
                    .stride = sizeof(VertexP4C4),
                    .format = Format::RGBA32_FLOAT
                }
            };
            auto gpipe = m_gpu->createGraphicsPipeline(gpd);

            // (Just gonna fake a render loop here)
            auto last = std::chrono::high_resolution_clock::now();
            while (!m_window->shouldClose()) {
                Window::pollEvents();

                auto now = std::chrono::high_resolution_clock::now();
                float dt_sec = std::chrono::duration<float>(now - last).count();
                last = now;

                Uniforms u{};
                u.t = dt_sec;
                u.N = VERT_COUNT;
                m_gpu->updateBuffer(ubuf, 0, sizeof(Uniforms), &u);

                ImageViewHandle bbView = m_gpu->acquireNextImage(swap);
                if (!bbView) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                // Compute pass
                ICommandList* clCompute = m_gpu->createCommandList(QueueType::COMPUTE);
                clCompute->begin();
                clCompute->bindComputePipeline(cpipe);
                clCompute->bindBindGroup(0, bgroup);
                const uint32_t WG = 64;
                uint32_t gx = (VERT_COUNT + WG - 1) / WG;
                clCompute->dispatch(gx, 1, 1);
                clCompute->end();

                // Render pass
                ICommandList* clRender = m_gpu->createCommandList(QueueType::GRAPHICS);
                clRender->begin();
                AttachmentDescriptor colorAttach{};
                colorAttach.view = bbView;
                colorAttach.load = AttachmentDescriptor::LoadOperation::CLEAR;
                colorAttach.store = AttachmentDescriptor::StoreOperation::STORE;
                colorAttach.clearColor = { 1.0f, 0.06f, 0.08f, 1.0f };
                RenderPassBeginInfo rp{};
                rp.colorAttachments = { colorAttach };
                clRender->beginRenderPass(rp);
                clRender->bindGraphicsPipeline(gpipe);
                std::vector<BufferHandle> vbs = { vbuf };
                std::vector<BufferHandle> vbs2 = { vbuf2 };
                std::vector<size_t> offsets = { 0 };
                clRender->bindVertexBuffers(0, vbs2, offsets);
                clRender->draw(VERT_COUNT2, 1, 0, 0);
                clRender->bindVertexBuffers(0, vbs, offsets);
                clRender->draw(VERT_COUNT, 1, 0, 0);
                clRender->endRenderPass();
                clRender->end();

                std::vector<ICommandList*> clSubmits = { clCompute, clRender };
                SubmitBatch batch{};
                batch.lists = std::span<ICommandList*>( clSubmits );
                m_gpu->submit(batch);
                m_gpu->present(swap);

                m_gpu->destroyImageView(bbView);
                m_gpu->destroyCommandList(clCompute);
                m_gpu->destroyCommandList(clRender);
            }

            m_gpu->destroyBuffer(vbuf);
            m_gpu->destroyBuffer(ubuf);
            m_gpu->destroyBindGroup(bgroup);
            m_gpu->destroyBindGroupLayout(bgl);
            m_gpu->destroyPipelineLayout(cpl);
            m_gpu->destroyPipelineLayout(gpl);
            m_gpu->destroyComputePipeline(cpipe);
            m_gpu->destroyGraphicsPipeline(gpipe);
            m_gpu->destroySwapchain(swap);
        }
            break;
    }
}

void App::update() {
    Window::pollEvents();

    static double lastFrame = glfwGetTime();
    double now = glfwGetTime();
    float dt = static_cast<float>(now - lastFrame);
    lastFrame = now;

    GLFWwindow* win = m_window->getGLFWwindow();
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camera.processKeyboard('W', dt);
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camera.processKeyboard('S', dt);
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) g_camera.processKeyboard('A', dt);
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_camera.processKeyboard('D', dt);


    switch (m_backend) {
        case RenderBackend::CUDA:
            // ImGui + present path (one swap inside endFrame)
            m_cudaTracer->drawFrame(g_camera, g_scene);
            m_rendererGL->beginFrame();
            m_rendererGL->setTexture(m_cudaTracer->getGLTex(),
                                    m_window->getWidth(),
                                    m_window->getHeight());
            m_rendererGL->drawFrame(g_camera, g_scene);
            m_rendererGL->endFrame();
            break;

        case RenderBackend::OpenGL:
            // ImGui + present path (one swap inside endFrame)
            m_rendererGL->beginFrame();
            m_rendererGL->drawFrame(g_camera, g_scene);
            m_rendererGL->endFrame();
            break;

        case RenderBackend::WEBGPU_D3D12:
        case RenderBackend::WEBGPU_VULKAN:
            break;
    }
}

void App::shutdown() {
    if (m_rendererGL) { m_rendererGL->shutdown(); m_rendererGL.reset(); }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }

    if (m_backend == RenderBackend::WEBGPU_D3D12 || m_backend == RenderBackend::WEBGPU_VULKAN) {
        m_gpu.reset();
        m_window.reset();
    }
}

} // namespace blok