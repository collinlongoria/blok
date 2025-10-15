/*
* File: main
* Project: blok
* Author: Collin Longoria
*
* Created on: 9/4/2025
*/

#include <iostream>
#include "app.hpp"
#include "backend.hpp"

int main() {
    try {
        blok::GraphicsApi backend = blok::GraphicsApi::Vulkan;

        blok::App app(backend);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
    return 0;
}