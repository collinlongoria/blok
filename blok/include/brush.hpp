/*
* File: brush.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef BRUSH_HPP
#define BRUSH_HPP
#include <vec3.hpp>

namespace blok {
class ChunkManager;

struct Brush {
    glm::vec3 centerWS;
    float radiusWS;
    float value;
    enum Mode { ADD, SUBTRACT } mode;
};

void applyBrush(ChunkManager& mgr, const Brush& brush);

}

#endif