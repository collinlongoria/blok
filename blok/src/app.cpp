/*
* File: app
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*/

#include "app.hpp"

using namespace blok;

void App::run() {
    init();

    while (!m_window->shouldClose()) {
        update();
    }

    shutdown();
}

void App::init() {
    m_window = std::make_shared<Window>(640, 480, "blok");
}

void App::update() {
    Window::pollEvents();
}

void App::shutdown() {

}
