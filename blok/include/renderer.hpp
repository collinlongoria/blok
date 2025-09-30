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

#include <array>
#include <cstdint>
#include <span>

#include "backend.hpp"
#include "gpu_handles.hpp"
#include "math.hpp"

struct GLFWwindow;
namespace blok {
class ICommandList;
class IGPUDevice;
class Window; }

namespace blok {
/*
 * LEGACY RENDERER INTERFACE
 * (will talk to yall about this)
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Initialization
    virtual void init() = 0;
    virtual void beginFrame() = 0;
    virtual void drawFrame() = 0;
    virtual void endFrame() = 0;
    virtual void shutdown() = 0;
};
}
#endif // RENDERER_HPP