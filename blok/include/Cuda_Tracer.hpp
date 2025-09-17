/*
* File: Cuda_Tracer
* Project: blok
* Author: Wes Morosan
* Created on: 9/12/2025
*
* Description: Raytracing core
*/
#pragma once

#include <cstdint>

// Forward declaration only
struct cudaGraphicsResource;

namespace blok {

    class CudaTracer {
    public:
        CudaTracer(unsigned int width, unsigned int height);
        ~CudaTracer();

        void init();

        void render();

        // GL texture that contains the CUDA output
        unsigned int getGLTex() const { return m_glTex; }

    private:
        void cleanup();

        unsigned int m_width = 0;
        unsigned int m_height = 0;

        // OpenGL objects
        unsigned int m_pbo = 0;   
        unsigned int m_glTex = 0; 

        // CUDA handle for the PBO
        cudaGraphicsResource* m_cudaPBO = nullptr;
    };

} // namespace blok
