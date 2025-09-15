/*
* File: main
* Project: blok
* Author: Collin Longoria
* Created on: 9/4/2025
*/

#include <iostream>

#include "app.hpp"

int main(){
    try {
        blok::App app;
        app.run();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}