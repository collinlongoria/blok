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

namespace blok {

    class IRenderer {
    public:
        virtual ~IRenderer() = default;
        virtual void init() = 0;
        virtual void beginFrame() = 0;
        virtual void drawFrame() = 0;
        virtual void endFrame() = 0;
        virtual void shutdown() = 0;
    };

} // namespace blok

#endif // RENDERER_HPP