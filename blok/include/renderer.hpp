/*
* File: Renderer
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: common interface
*/
#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <cstdint>

#include "backend.hpp"
#include "math.hpp"

namespace blok {

struct RendererInitInfo {
    void* nativeWindowHandle = nullptr;
    uint32_t windowWidth = 0, windowHeight = 0;
    RenderBackend renderBackend = RenderBackend::WEBGPU_D3D12; // by default?
};

// TODO: make this better
struct FrameData {
    Matrix4 view = Matrix4(1.0f);
    Matrix4 projection = Matrix4(1.0f);
    Vector3 eyePos = Vector3(0.0f);
};

struct RenderCommand {
    // TODO: populate. need to consider depth passes, etc?
};

struct ComputeCommand {
    // TODO: populate
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Initialization
    virtual void init() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Frame Lifecycle
    virtual void setFrameData(const FrameData& fd) = 0;
    virtual void beginFrame() = 0;
    virtual void submitRenderCommand(const RenderCommand& cmd) = 0;
    virtual void submitComputeCommand(const ComputeCommand& cmd) = 0;
    virtual void endFrame() = 0;

    virtual void shutdown() = 0;
};

} // namespace blok

#endif // RENDERER_HPP