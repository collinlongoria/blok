/*
* File: noise.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/20/2025
*/

#ifndef NOISE_HPP
#define NOISE_HPP

#include <cstdint>

// simple hash function for noise
inline float hash(int x, int y, uint32_t seed) {
    int n = x + y * 57 + seed * 131;
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

// smooth interpolation (smoothstep)
inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// 2D value noise
float valueNoise2D(float x, float y, uint32_t seed) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));

    float xf = x - xi;
    float yf = y - yi;

    // get values at corners
    float v00 = hash(xi, yi, seed);
    float v10 = hash(xi + 1, yi, seed);
    float v01 = hash(xi, yi + 1, seed);
    float v11 = hash(xi + 1, yi + 1, seed);

    // smooth interpolation
    float sx = smoothstep(xf);
    float sy = smoothstep(yf);

    // bilinear interpolation
    float v0 = v00 + sx * (v10 - v00);
    float v1 = v01 + sx * (v11 - v01);

    return v0 + sy * (v1 - v0);
}

// fractal brownian motion (layered noise)
float fbm2D(float x, float y, uint32_t seed, int octaves = 4, float lacunarity = 2.0f, float persistence = 0.5f) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * valueNoise2D(x * frequency, y * frequency, seed + i * 1000);
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxValue; // normalized to -1,1
}

// ridged noise for mountain ridges
float ridgedNoise2D(float x, float y, uint32_t seed, int octaves = 4) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        float n = valueNoise2D(x * frequency, y * frequency, seed + i * 1000);
        n = 1.0f - std::abs(n); // create ridges
        n = n * n; // sharpen ridges
        value += amplitude * n;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value / maxValue;
}

#endif //NOISE_HPP