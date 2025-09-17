#include "Renderer_WGPU.hpp"
#include <iostream>

using namespace blok;

RendererWGPU::RendererWGPU(std::shared_ptr<Window> window) : m_window(window) {}
RendererWGPU::~RendererWGPU() {}

void RendererWGPU::init() {
    std::cout << "[WebGPU] init stub\n";
}

void RendererWGPU::drawFrame() {
    std::cout << "[WebGPU] drawFrame stub\n";
}

void RendererWGPU::shutdown() {
    std::cout << "[WebGPU] shutdown stub\n";
}