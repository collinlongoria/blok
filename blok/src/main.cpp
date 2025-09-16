/*
* File: main
* Project: blok
* Author: Collin Longoria
* Created on: 9/4/2025
*/

#include <webgpu.h>
#include <iostream>

#include <memory>

#include "math.hpp"
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

    Blok::Vector2 a(1,1);
    Blok::Vector2 b(2,2);

    auto c = a + b;
    std::cout << "(" << c.x << ", " << c.y << ")" << std::endl;


    Blok::Transform t;

    std::cout << "pos = (" << t.translation.x << ", "<< t.translation.y << ", " << t.translation.z << ")" << std::endl;
    std::cout << "scale = (" << t.scale.x << ", " << t.scale.y << ", " << t.scale.z << ")" << std::endl;
    std::cout << "rot = (" << t.rotation.x << ", " << t.rotation.y << ", " << t.rotation.z << ", " << t.rotation.w << ")" << std::endl;

    Blok::Vector3 z(0, 0, 1);
    t.rotate(45, z);
    std::cout << "rot = (" << t.rotation.x << ", " << t.rotation.y << ", " << t.rotation.z << ", " << t.rotation.w << ")" << std::endl;

    // Window test
    std::unique_ptr<blok::Window> window = std::make_unique<blok::Window>(640, 480, "blok");

    while(!window->shouldClose()) {
        blok::Window::pollEvents(); // Don't like this, but it doesn't need to be per window so IDK.
    }

    // Cleanup
    wgpuInstanceRelease(instance);
    return 0;
}