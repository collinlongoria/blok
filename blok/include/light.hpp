/*
* File: light
* Project: blok
* Author: Collin Longoria
* Created on: 11/10/2025
*
* Description:
*/
#ifndef BLOK_LIGHT_HPP
#define BLOK_LIGHT_HPP
#include "math.hpp"

#define MAX_LIGHTS 10
namespace blok {
enum LightType {
    POINT,
    SPOT,
    DIRECTIONAL
};

// TODO: Consider a GPU side implementationt that packs into vec4 alignments (example: position.xyz, radius.w)
struct Light {
    LightType type;

    Vector3 position; // for POINT and SPOT
    float radius; // attenuation range

    Vector3 direction; // for SPOT and DIRECTIONAL
    float cutoff; // inner/outer angle (in radians)

    Vector3 color;
    float intensity; // brightness multipler
};

// Constructor helpers
static Light Point(Vector3 pos, Vector3 color, float intensity, float radius = 10.0f) {
    return { LightType::POINT, pos, radius, {}, 0.0f, color, intensity};
}

static Light Spot(Vector3 pos, Vector3 dir, Vector3 color, float intensity, float cutoff, float radius = 10.0f) {
    return { LightType::SPOT, pos, radius, dir, cutoff, color, intensity};
}

static Light Directional(Vector3 dir, Vector3 color, float intensity) {
    return { LightType::DIRECTIONAL, {}, 0.0f, dir, 0.0f, color, intensity};
}

}

#endif //BLOK_LIGHT_HPP