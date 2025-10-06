/*
* File: Renderer
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: common interface
*/
#ifndef RENDERER_HPP
#define RENDERER_HPP
#include "camera.hpp"
#include "scene.hpp"
namespace blok {

class Camera;
struct Scene;

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void init() = 0;
    virtual void shutdown() = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    // Fix signature to match derived classes
    virtual void drawFrame(const Camera& cam, const Scene& scene) = 0;
};

} // namespace blok

#endif