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

#include <array>
#include <cstdint>
#include <span>

#include "backend.hpp"
#include "math.hpp"

struct GLFWwindow;
namespace blok { class Window; }

namespace blok {
/*
 * LEGACY RENDERER INTERFACE
 * (will talk to yall about this)
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Initialization
    virtual void init() = 0;
    virtual void beginFrame() = 0;
    virtual void drawFrame() = 0;
    virtual void endFrame() = 0;
    virtual void shutdown() = 0;
};

/*
 * Types
 */

// I am moving this here for simplicity, it does not need to be in its own file...?
enum class Backend {
    D3D12,
    VULKAN,
    OPENGL
};

// Essentially the same from gpu_types - trying not to expose that.
enum class ShaderIR {
    GLSL,
    WGSL,
    HLSL,
    SPIRV
};

// Also defined similarly in gpu_types
// (breaking style convention here for readability)
enum class VertexFormat {
    Float32x2,
    Float32x3,
    Float32x4,
    UNorm8x4,
    SNorm8x4,
    UInt8x4,
    SInt8x4,
    UNorm16x2,
    UNorm16x4,
    Float16x2,
    Float16x4,
};

// TODO: implement this inside the device
enum class VertexStep {
    PerVertex,
    PerInstance
};

struct VertexAttributeDesc {
    uint32_t location{0};
    VertexFormat format{VertexFormat::Float32x4};
    uint32_t offsetBytes{0};
};

struct VertexBufferLayoutDesc {
    uint32_t binding{0};
    uint32_t strideBytes{0};
    VertexStep step{VertexStep::PerVertex};
    std::vector<VertexAttributeDesc> attributes{};
};

class VertexLayout {
public:
    template<typename V>
    static VertexLayout forStruct() {
        VertexLayout layout;
        layout.m_buffers.push_back(VertexBufferLayoutDesc{.binding=0, .strideBytes=static_cast<uint32_t>(sizeof(V))});
        return layout;
    }
    VertexLayout& buffer(uint32_t binding, uint32_t strideBytes, VertexStep step = VertexStep::PerVertex) {
        m_buffers.push_back(VertexBufferLayoutDesc{binding, strideBytes, step, {}});
        return *this;
    }
    VertexLayout& attr(uint32_t location, VertexFormat format, uint32_t offsetBytes, uint32_t bufferIndex = 0) {
        if (bufferIndex >= m_buffers.size()) m_buffers.resize(bufferIndex + 1);
        m_buffers[bufferIndex].attributes.push_back(VertexAttributeDesc{location, format, offsetBytes});
        return *this;
    }
    [[nodiscard]] VertexLayout finalize() const { return *this;}

    [[nodiscard]] const std::vector<VertexBufferLayoutDesc>& buffers() const { return m_buffers; }
private:
    std::vector<VertexBufferLayoutDesc> m_buffers{};
};

struct TextureDesc {
    uint32_t width{0}, height{0};
    uint32_t mipLevels{1};
    bool srgb{true};
};

struct ClearColor { float r{0}, g{0}, b{0}, a{0}; };

struct RendererInit {
    std::shared_ptr<Window> window;
    bool vsync{true}; // TODO: change device to check for and prioritize mailbox->vsync
    // TODO: allow for turning on/off srgb in device bool useSRGB{true};
};

struct GraphicsShaderBytes {
    std::span<const uint8_t> vs;
    std::span<const uint8_t> fs;
    ShaderIR ir{ShaderIR::SPIRV};
};
struct ComputeShaderBytes {
    std::span<const uint8_t> cs;
    ShaderIR ir{ShaderIR::SPIRV};
};

/*
 * "front-end" handles for the gpu_types.
 * Again, trying not to expose gpu_ headers,
 * this is the (cumbersome) solution.
 * Have a better idea? please let me know.
 */
struct MeshId { uint64_t id{0}; explicit operator bool() const { return id!=0; } };
struct MaterialId { uint64_t id{0}; explicit operator bool() const { return id!=0; } };
struct KernelId { uint64_t id{0}; explicit operator bool() const { return id!=0; } };
struct TextureId { uint64_t id{0}; explicit operator bool() const { return id!=0; } };

/*
 * Frame and Render passes
 */
class RenderPass;

class Frame {
public:
    Frame() = default;
    explicit operator bool() const { return m_ok; }

    // Compute
    void dispatch(KernelId kernel, std::array<uint32_t, 3> workGroups);

    // Graphics
    RenderPass renderToBackbuffer(const ClearColor& clear = {});

    // Present
    void end();

private:
    friend class Renderer;
    explicit Frame(class Renderer* R) : m_renderer(R) {}
    class Renderer* m_renderer = nullptr;
    bool m_ok = false;
};

class RenderPass {
public:
    RenderPass() = default;
    void draw(MeshId mesh, MaterialId mat, uint32_t instances = 1);
    void end();
private:
    friend class Renderer;
    explicit RenderPass(class Renderer* R) : m_renderer(R) {}
    class Renderer* m_renderer = nullptr;
};

/*
 * Renderer
 */
class Renderer {
public:
    static std::unique_ptr<Renderer> create(const RendererInit& init);

    virtual ~Renderer() = default;

    // Per-frame
    virtual Frame beginFrame() = 0;

    // Resources
    struct MeshData { std::span<const std::byte> bytes; uint32_t vertexCount; };
    virtual MeshId createMesh(MeshData data, const VertexLayout& layout) = 0;
    virtual TextureId createTexture2D(const TextureDesc&, std::span<const std::byte> pixels) = 0;
    virtual MaterialId createMaterial(const GraphicsShaderBytes& shaders, const VertexLayout& layout) = 0;
    virtual KernelId createKernel(const ComputeShaderBytes& shaders) = 0;

protected:
    Renderer() = default;


    virtual void _dispatch(KernelId, const std::array<uint32_t,3>&) = 0;
    virtual RenderPass _beginBackbufferPass(const ClearColor&) = 0;
    virtual void _passDraw(RenderPass&, MeshId, MaterialId, uint32_t) = 0;
    virtual void _passEnd(RenderPass&) = 0;
    virtual void _frameEnd() = 0; // submit + present + per-frame cleanup

    // Allow Frame/RenderPass to call the hooks
    friend class Frame;
    friend class RenderPass;
};

// ----------------------------- Inline glue for Frame/Pass -----------------------------
inline void Frame::dispatch(KernelId kernel, std::array<uint32_t,3> groups) {
    if (m_renderer) m_renderer->_dispatch(kernel, groups);
}
inline RenderPass Frame::renderToBackbuffer(const ClearColor& clear) {
    return m_renderer ? m_renderer->_beginBackbufferPass(clear) : RenderPass{};
}
inline void Frame::end() { if (m_renderer) m_renderer->_frameEnd(); m_ok = false; }


inline void RenderPass::draw(MeshId mesh, MaterialId mat, uint32_t instances) {
    if (m_renderer) m_renderer->_passDraw(*this, mesh, mat, instances);
}
inline void RenderPass::end() { if (m_renderer) m_renderer->_passEnd(*this); }
} // namespace blok

#endif // RENDERER_HPP