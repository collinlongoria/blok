#ifndef RENDERER_WGPU_HPP
#define RENDERER_WGPU_HPP

#include "Renderer.hpp"
#include "window.hpp"
#include <memory>

namespace blok {

    class RendererWGPU : public IRenderer {
    public:
        explicit RendererWGPU(std::shared_ptr<Window> window);
        ~RendererWGPU() override;

        void init() override;
        void drawFrame(const Camera& cam, const Scene& scene) override;
        void shutdown() override;

    private:
        std::shared_ptr<Window> m_window;
    };

} // namespace blok

#endif