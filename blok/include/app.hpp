/*
* File: app
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*/
#ifndef BLOK_APP_HPP
#define BLOK_APP_HPP
#include <memory>

#include "Window.hpp"

namespace blok {
    class App {
    public:
        App() = default;
        ~App() = default;

        void run(); // Main entry point
    private:
        void init();
        void update();
        void shutdown();

        std::shared_ptr<Window> window;
    };
}

#endif //BLOK_APP_HPP