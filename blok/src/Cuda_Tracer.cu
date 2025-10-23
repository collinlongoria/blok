/*
* File: Renderer_GL
* Project: blok
* Author: Wes Morosan
* Created on: 9/10/2025
* Description: Primarily responsible for raytracing
*/

#include "Cuda_Tracer.hpp"
#include "camera.hpp"
#include "scene.hpp"

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <stdexcept>
#include <iostream>

using namespace blok;

struct SphereCUDA {
    float3 center;
    float  radius;
    uchar4 color;
};

struct PlaneCUDA {
    float3 normal;
    float  d;
    uchar4 color;
};

struct CameraCUDA {
    float3 pos;
    float3 forward;
    float3 right;
    float3 up;
    float  fovScale;
};

// Utility
__device__ float3 normalize3(const float3& v) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    return make_float3(v.x/len, v.y/len, v.z/len);
}
__device__ float dot3(const float3& a, const float3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
__device__ float3 add3(const float3& a, const float3& b) {
    return make_float3(a.x+b.x, a.y+b.y, a.z+b.z);
}
__device__ float3 sub3(const float3& a, const float3& b) {
    return make_float3(a.x-b.x, a.y-b.y, a.z-b.z);
}
__device__ float3 mul3(const float3& a, float s) {
    return make_float3(a.x*s, a.y*s, a.z*s);
}

// Intersections
__device__ bool hit_sphere(const SphereCUDA& s, float3 ro, float3 rd, float& tHit) {
    float3 oc = sub3(ro, s.center);
    float a = dot3(rd, rd);
    float b = 2.0f * dot3(oc, rd);
    float c = dot3(oc, oc) - s.radius*s.radius;
    float disc = b*b - 4*a*c;
    if (disc < 0) return false;
    float t0 = (-b - sqrtf(disc)) / (2.0f*a);
    float t1 = (-b + sqrtf(disc)) / (2.0f*a);
    tHit = (t0 > 1e-4f) ? t0 : ((t1 > 1e-4f) ? t1 : -1);
    return tHit > 0;
}

__device__ bool hit_plane(const PlaneCUDA& p, float3 ro, float3 rd, float& tHit) {
    float denom = dot3(p.normal, rd);
    if (fabsf(denom) < 1e-6f) return false;
    float t = -(dot3(p.normal, ro) + p.d) / denom;
    if (t > 1e-4f) { tHit = t; return true; }
    return false;
}

struct HitInfo {
    float  t;
    float3 n;
    uchar4 base;
    bool   hit;
};

__device__ uchar4 lerpU8(uchar4 a, uchar4 b, float t) {
    float it = 1.0f - t;
    return make_uchar4(
        (unsigned char)(a.x*it + b.x*t),
        (unsigned char)(a.y*it + b.y*t),
        (unsigned char)(a.z*it + b.z*t),
        255
    );
}

__device__ HitInfo traceClosest(
    float3 ro, float3 rd,
    SphereCUDA* spheres, int numSpheres,
    PlaneCUDA* planes,  int numPlanes)
{
    HitInfo h; h.t = 1e20f; h.hit = false; h.n = make_float3(0,1,0); h.base = make_uchar4(255,255,255,255);

    // spheres
    for (int i=0; i<numSpheres; ++i) {
        float tHit;
        if (hit_sphere(spheres[i], ro, rd, tHit) && tHit < h.t) {
            h.t = tHit;
            float3 hp = add3(ro, mul3(rd, tHit));
            h.n = normalize3(sub3(hp, spheres[i].center));
            h.base = spheres[i].color;
            h.hit = true;
        }
    }

    // planes (checkerboard)
    for (int i=0; i<numPlanes; ++i) {
        float tHit;
        if (hit_plane(planes[i], ro, rd, tHit) && tHit < h.t) {
            h.t = tHit;
            h.n = normalize3(planes[i].normal);

            // Checker: project hit point onto plane basis
            float3 hp = add3(ro, mul3(rd, tHit));
            // Build a tangent basis for the plane
            float3 n = h.n;
            float3 t = normalize3(fabsf(n.x) > 0.5f ? make_float3(0,1,0) : make_float3(1,0,0));
            t = normalize3(sub3(t, mul3(n, dot3(t,n))));
            float3 b = normalize3(make_float3(
                n.y*t.z - n.z*t.y,
                n.z*t.x - n.x*t.z,
                n.x*t.y - n.y*t.x));

            float u = dot3(hp, t);
            float v = dot3(hp, b);
            int iu = (int)floorf(u);
            int iv = (int)floorf(v);
            bool checker = ((iu + iv) & 1) == 0;

            uchar4 c0 = planes[i].color;              // base color
            uchar4 c1 = make_uchar4(220,220,220,255); // alternate
            h.base = checker ? c0 : c1;
            h.hit = true;
        }
    }

    return h;
}

__device__ bool traceShadow(
    float3 ro, float3 rd, float maxDist,
    SphereCUDA* spheres, int numSpheres,
    PlaneCUDA* planes,  int numPlanes)
{
    float t;
    ro = add3(ro, mul3(rd, 1e-3f));
    for (int i=0; i<numSpheres; ++i) {
        if (hit_sphere(spheres[i], ro, rd, t) && t < maxDist) return true;
    }
    for (int i=0; i<numPlanes; ++i) {
        if (hit_plane(planes[i], ro, rd, t) && t < maxDist) return true;
    }
    return false;
}

__device__ float3 skyGradient(float3 rd) {
    float t = 0.5f*(rd.y + 1.0f);
    float3 top = make_float3(0.5f, 0.7f, 1.0f);
    float3 bot = make_float3(1.0f, 1.0f, 1.0f);
    return add3(mul3(bot, (1.0f - t)), mul3(top, t));
}

__device__ unsigned char toSRGB8(float x) {
    x = fminf(fmaxf(x, 0.0f), 1.0f);
    float g = powf(x, 1.0f/2.2f);
    return (unsigned char)(g * 255.0f + 0.5f);
}

__device__ uchar4 shadePixel(
    float3 ro, float3 rd,
    SphereCUDA* spheres, int numSpheres,
    PlaneCUDA* planes,  int numPlanes,
    float3 lightDir)
{
    HitInfo h = traceClosest(ro, rd, spheres, numSpheres, planes, numPlanes);
    if (!h.hit) {
        float3 c = skyGradient(rd);
        return make_uchar4(toSRGB8(c.x), toSRGB8(c.y), toSRGB8(c.z), 255);
    }

    float3 hp = add3(ro, mul3(rd, h.t));
    float3 n  = h.n;
    float3 l  = normalize3(lightDir);
    float3 v  = normalize3(mul3(rd, -1.0f));
    float3 hlf= normalize3(add3(l, v));

    float ambient = 0.08f;
    float diff    = fmaxf(0.0f, dot3(n, l));
    float spec    = powf(fmaxf(0.0f, dot3(n, hlf)), 48.0f);

    bool shadowed = traceShadow(hp, l, 1e4f, spheres, numSpheres, planes, numPlanes);
    float shadowFactor = shadowed ? 0.25f : 1.0f;

    float3 base = make_float3(h.base.x/255.0f, h.base.y/255.0f, h.base.z/255.0f);

    float3 lit = add3(mul3(base, ambient + diff * shadowFactor),
                      mul3(make_float3(1.0f,1.0f,1.0f), spec * 0.4f * shadowFactor));

    return make_uchar4(toSRGB8(lit.x), toSRGB8(lit.y), toSRGB8(lit.z), 255);
}

// Kernel
__global__ void raytrace_kernel(
    uchar4* pixels, int width, int height,
    CameraCUDA cam,
    SphereCUDA* spheres, int numSpheres,
    PlaneCUDA* planes, int numPlanes,
    float tFrame)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    int idx = y * width + x;

    float u = (2.0f * ((x + 0.5f) / width) - 1.0f) * cam.fovScale;
    float v = (1.0f - 2.0f * ((y + 0.5f) / height)) * cam.fovScale;
    float3 rd = normalize3(add3(add3(cam.forward, mul3(cam.right, u)), mul3(cam.up, v)));
    float3 ro = cam.pos;

    uchar4 color = make_uchar4(100, 149, 237, 255);

    float3 lightDir = normalize3(make_float3(cosf(0.3f + tFrame)*0.6f + 1.0f, 1.0f, -0.5f));

    color = shadePixel(ro, rd, spheres, numSpheres, planes, numPlanes, lightDir);

    pixels[idx] = color;
}

// Helpers
static SphereCUDA toDevice(const Sphere& s) {
    return {
        make_float3(s.center.x, s.center.y, s.center.z),
        s.radius,
        make_uchar4((uint8_t)(s.color.r*255),
                    (uint8_t)(s.color.g*255),
                    (uint8_t)(s.color.b*255),
                    255)
    };
}

static PlaneCUDA toDevice(const Plane& p) {
    return {
        make_float3(p.normal.x, p.normal.y, p.normal.z),
        p.d,
        make_uchar4((uint8_t)(p.color.r*255),
                    (uint8_t)(p.color.g*255),
                    (uint8_t)(p.color.b*255),
                    255)
    };
}

static CameraCUDA toDevice(const Camera& c, int width, int height) {
    float aspect = (float)width / (float)height;
    float fovScale = tanf(0.5f * c.fov * 3.14159f/180.0f);
    return {
        make_float3(c.position.x, c.position.y, c.position.z),
        make_float3(c.forward().x, c.forward().y, c.forward().z),
        make_float3(c.right().x,   c.right().y,   c.right().z),
        make_float3(c.up().x,      c.up().y,      c.up().z),
        fovScale
    };
}

CudaTracer::CudaTracer(unsigned int width, unsigned int height)
    : m_width(width), m_height(height) {}

CudaTracer::~CudaTracer() {
    cleanup();
}

void CudaTracer::init() {
    glGenBuffers(1, &m_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,
                 static_cast<GLsizeiptr>(m_width) * m_height * 4,
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    if (cudaGraphicsGLRegisterBuffer(&m_cudaPBO, m_pbo,
                                     cudaGraphicsMapFlagsWriteDiscard) != cudaSuccess) {
        throw std::runtime_error("Failed to register PBO with CUDA");
    }

    glGenTextures(1, &m_glTex);
    glBindTexture(GL_TEXTURE_2D, m_glTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(m_width),
                 static_cast<GLsizei>(m_height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CudaTracer::drawFrame(Camera& cam, const Scene& scene) {
    std::vector<SphereCUDA> spheres;
    for (auto& s : scene.spheres) spheres.push_back(toDevice(s));
    std::vector<PlaneCUDA> planes;
    for (auto& p : scene.planes) planes.push_back(toDevice(p));

    SphereCUDA* dSpheres = nullptr;
    PlaneCUDA* dPlanes   = nullptr;
    if (!spheres.empty())
        cudaMalloc(&dSpheres, spheres.size() * sizeof(SphereCUDA));
    if (!planes.empty())
        cudaMalloc(&dPlanes, planes.size() * sizeof(PlaneCUDA));

    if (!spheres.empty())
        cudaMemcpy(dSpheres, spheres.data(),
                   spheres.size() * sizeof(SphereCUDA),
                   cudaMemcpyHostToDevice);
    if (!planes.empty())
        cudaMemcpy(dPlanes, planes.data(),
                   planes.size() * sizeof(PlaneCUDA),
                   cudaMemcpyHostToDevice);

    CameraCUDA dCam = toDevice(cam, m_width, m_height);

    uchar4* devPtr = nullptr;
    size_t size;
    cudaGraphicsMapResources(1, &m_cudaPBO, 0);
    cudaGraphicsResourceGetMappedPointer((void**)&devPtr, &size, m_cudaPBO);

    dim3 block(16, 16);
    dim3 grid((m_width + 15) / 16, (m_height + 15) / 16);

    static float t = 0.0f;
    t += 0.01f;

    raytrace_kernel<<<grid, block>>>(devPtr, m_width, m_height, dCam,
                                     dSpheres, (int)spheres.size(),
                                     dPlanes, (int)planes.size(),
                                     t);
    cudaDeviceSynchronize();

    cudaGraphicsUnmapResources(1, &m_cudaPBO, 0);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
    glBindTexture(GL_TEXTURE_2D, m_glTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0,
                    static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height),
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    if (dSpheres) cudaFree(dSpheres);
    if (dPlanes)  cudaFree(dPlanes);
}

void CudaTracer::beginFrame() {}
void CudaTracer::endFrame() {}

void CudaTracer::shutdown() {
    cleanup();
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