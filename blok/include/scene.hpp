/*
* File: scene
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: Simple scene with spheres and planes
*/
#ifndef SCENE_HPP
#define SCENE_HPP  
#include <vector>

namespace blok {

struct Color {
    float r, g, b;
};

struct Sphere {
    struct { float x, y, z; } center;
    float radius;
    Color color;
};

struct Plane {
    struct { float x, y, z; } normal;
    float d;    
    Color color;
};

struct Scene {
    std::vector<Sphere> spheres;
    std::vector<Plane>  planes;

    Scene() {

        spheres.push_back({
            {-2.0f, 0.0f, -3.0f},   
            1.0f,                  
            {1.0f, 0.0f, 0.0f}  
        });

        spheres.push_back({
            {-2.0f, 0.0f, -8.0f},   
            1.0f,                  
            {1.0f, 1.0f, 0.0f}  
        });


        spheres.push_back({
            {-2.0f, 0.0f, -13.0f},  
            1.0f,                  
            {0.0f, 0.0f, 1.0f}  
        });

        spheres.push_back({
            {-2.0f, 0.0f, -18.0f},  
            1.0f,                  
            {0.0f, 1.0f, 1.0f}  
        });

        planes.push_back({
            {0.0f, 1.0f, 0.0f},    
            1.0f,                  
            {0.3f, 0.8f, 0.3f}
        });
    }
};

} // namespace blok
#endif // SCENE_HPP