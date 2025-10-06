#include "webgpu_renderer.hpp"
#include <iostream>

using namespace blok;

RendererWGPU::RendererWGPU(std::shared_ptr<Window> window) : m_window(window) {}
RendererWGPU::~RendererWGPU() {}

void RendererWGPU::init() {
    std::cout << "[WebGPU] init stub\n";
}

void RendererWGPU::drawFrame(const Camera& cam, const Scene& scene) {
    (void)cam;   // unused for now
    (void)scene; // unused for now
}

void RendererWGPU::shutdown() {
    std::cout << "[WebGPU] shutdown stub\n";
}