/*
* File: Renderer_GL
* Project: blok
* Author: Wes Morosan
* Created on: 9/10/2025
* Description: Primarily responsible for raytracing
*/

#include "Cuda_Tracer.hpp"

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

#include <cmath>
#include <stdexcept>
#include <iostream>

using namespace blok;

// fills pixels with a moving gradient
__global__ void raytrace_kernel(uchar4* pixels, int width, int height, float t) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int idx = y * width + x;

    float u = (float)x / (float)width;
    float v = (float)y / (float)height;

    unsigned char r = static_cast<unsigned char>(u * 255.0f);
    unsigned char g = static_cast<unsigned char>(v * 255.0f);
    unsigned char b = static_cast<unsigned char>((0.5f + 0.5f * sinf(t)) * 255.0f);

    pixels[idx] = make_uchar4(r, g, b, 255);
}

CudaTracer::CudaTracer(unsigned int width, unsigned int height)
    : m_width(width), m_height(height) {}

CudaTracer::~CudaTracer() {
    cleanup();
}

void CudaTracer::init() {

    glGenBuffers(1, &m_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(m_width) * m_height * 4, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    {
        cudaError_t err = cudaGraphicsGLRegisterBuffer(&m_cudaPBO, m_pbo, cudaGraphicsMapFlagsWriteDiscard);
        if (err != cudaSuccess) {
            throw std::runtime_error("Failed to register PBO with CUDA");
        }
    }

    glGenTextures(1, &m_glTex);
    glBindTexture(GL_TEXTURE_2D, m_glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // no filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CudaTracer::render() {
    if (!m_cudaPBO || !m_pbo || !m_glTex) return;

    uchar4* devPtr = nullptr;
    size_t mappedSize = 0;
    {
        cudaError_t err = cudaGraphicsMapResources(1, &m_cudaPBO, 0);
        if (err != cudaSuccess) {
            std::cerr << "cudaGraphicsMapResources failed: " << static_cast<int>(err) << "\n";
            return;
        }
        err = cudaGraphicsResourceGetMappedPointer(reinterpret_cast<void**>(&devPtr), &mappedSize, m_cudaPBO);
        if (err != cudaSuccess || !devPtr) {
            std::cerr << "cudaGraphicsResourceGetMappedPointer failed\n";
            cudaGraphicsUnmapResources(1, &m_cudaPBO, 0);
            return;
        }
    }

    // Launch kernel to fill PBO
    dim3 block(16, 16);
    dim3 grid((m_width + block.x - 1) / block.x,
              (m_height + block.y - 1) / block.y);

    static float t = 0.0f;
    t += 0.02f;

    raytrace_kernel<<<grid, block>>>(devPtr, static_cast<int>(m_width), static_cast<int>(m_height), t);
    cudaDeviceSynchronize();

    cudaGraphicsUnmapResources(1, &m_cudaPBO, 0);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
    glBindTexture(GL_TEXTURE_2D, m_glTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0,
                    static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height),
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CudaTracer::cleanup() {
    if (m_cudaPBO) {
        cudaGraphicsUnregisterResource(m_cudaPBO);
        m_cudaPBO = nullptr;
    }
    if (m_pbo) {
        glDeleteBuffers(1, &m_pbo);
        m_pbo = 0;
    }
    if (m_glTex) {
        glDeleteTextures(1, &m_glTex);
        m_glTex = 0;
    }
}
