/*
* File: main
* Project: blok
* Author: Collin Longoria
* Created on: 9/4/2025
*/

#include <webgpu.h>
#include <iostream>

int main(void){
    // Create WebGPU instance
    // Create a descriptor
    WGPUInstanceDescriptor desc = {};
    WGPUInstance instance = wgpuCreateInstance(&desc);
    if(!instance) {
        std::cerr << "Failed to create WGPU instance" << std::endl;
    }

    std::cout << "blok." << std::endl;
    return 0;
}