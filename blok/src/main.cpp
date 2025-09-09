/*
* File: main
* Project: blok
* Author: Collin Longoria
* Created on: 9/4/2025
*/

#include <webgpu.h>
#include <iostream>

#include <glm.hpp>
#include <memory>

#include "window.hpp"

int main(void){
    // Create WebGPU instance
    // Create a descriptor
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;

    // Create instance using descriptor
    WGPUInstance instance = wgpuCreateInstance(&desc);
    if(!instance) {
        std::cerr << "Failed to create WGPU instance" << std::endl;
        return 1;
    }
    std::cout << "WGPU instance: " << instance << std::endl;

    glm::vec2 a(1,1);
    glm::vec2 b(2,2);

    auto c = a + b;
    std::cout << "(" << c.x << ", " << c.y << ")" << std::endl;

    // Window test
    std::unique_ptr<blok::Window> window = std::make_unique<blok::Window>(640, 480, "blok");

    while(!window->shouldClose()) {
        blok::Window::pollEvents(); // Don't like this, but it doesn't need to be per window so IDK.
    }

    // Cleanup
    wgpuInstanceRelease(instance);
    return 0;
}