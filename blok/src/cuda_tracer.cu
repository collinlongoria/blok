/*
* File: Renderer_GL
* Project: blok
* Author: Wes Morosan
* Created on: 9/10/2025
* Description: Primarily responsible for raytracing
*/
#define VULKAN_HPP_NO_TO_STRING

#include "cuda_tracer.hpp"
#include "camera.hpp"
#include "scene.hpp"

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>


#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

#include <stdexcept>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace blok;

// device-side material system
enum MaterialTypeCUDA { MAT_LAMBERT=0, MAT_MIRROR=1, MAT_DIELECTRIC=2 };

struct MaterialCUDA {
    int     type;
    float3  albedo;
    float   eta;       // IOR
    float3  emission;  // emissive radiance
    float   _pad;
};

// geometry references materials by id
struct SphereCUDA {
    float3 center;
    float  radius;
    int    matId;
};

struct PlaneCUDA {
    float3 normal;
    float  d;
    int    matId;
};

struct CameraCUDA {
    float3 pos;
    float3 forward;
    float3 right;
    float3 up;
    float  fovScale;
    float  aspect;
};

// math
__device__ inline float3 normalize3(const float3& v) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    return make_float3(v.x/len, v.y/len, v.z/len);
}
__device__ inline float dot3(const float3& a, const float3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
__device__ inline float3 add3(const float3& a, const float3& b) {
    return make_float3(a.x+b.x, a.y+b.y, a.z+b.z);
}
__device__ inline float3 sub3(const float3& a, const float3& b) {
    return make_float3(a.x-b.x, a.y-b.y, a.z-b.z);
}
__device__ inline float3 mul3(const float3& a, float s) {
    return make_float3(a.x*s, a.y*s, a.z*s);
}
__device__ inline float3 mul3(const float3& a, const float3& b) {
    return make_float3(a.x*b.x, a.y*b.y, a.z*b.z);
}
__device__ inline float3 reflect3(const float3& v, const float3& n) {
    return sub3(v, mul3(n, 2.f * dot3(v,n)));
}
__device__ inline bool refract3(const float3& v, const float3& n, float eta, float3& t) {
    float cosi = -dot3(v, n);
    float cost2 = 1.f - eta*eta*(1.f - cosi*cosi);
    if (cost2 < 0.f) return false;
    t = add3(mul3(v, eta), mul3(n, eta*cosi - sqrtf(cost2)));
    return true;
}
__device__ inline float schlick(float cosi, float eta) {
    float r0 = (1.f - eta) / (1.f + eta);
    r0 = r0*r0;
    return r0 + (1.f - r0)*powf(1.f - cosi, 5.f);
}
__device__ inline unsigned char toSRGB8(float x) {
    x = fminf(fmaxf(x, 0.0f), 1.0f);
    float g = powf(x, 1.0f/2.2f);
    return (unsigned char)(g * 255.0f + 0.5f);
}

// rng
struct RNG {
    uint32_t state;
    __device__ RNG(uint32_t seed) : state(seed) {}
    __device__ inline uint32_t u32(){
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x; return x;
    }
    __device__ inline float f32(){
        return (u32() & 0x007FFFFF) * (1.0f / 8388607.0f);
    }
};

// intersections
__device__ bool hit_sphere(const SphereCUDA& s, float3 ro, float3 rd, float& tHit, float3& nOut, int& matId) {
    float3 oc = sub3(ro, s.center);
    float a = dot3(rd, rd);
    float b = 2.0f * dot3(oc, rd);
    float c = dot3(oc, oc) - s.radius*s.radius;
    float disc = b*b - 4*a*c;
    if (disc < 0) return false;
    float t0 = (-b - sqrtf(disc)) / (2.0f*a);
    float t1 = (-b + sqrtf(disc)) / (2.0f*a);
    float t  = (t0 > 1e-4f) ? t0 : ((t1 > 1e-4f) ? t1 : -1);
    if (t <= 0) return false;
    tHit = t;
    float3 hp = add3(ro, mul3(rd, t));
    nOut = normalize3(sub3(hp, s.center));
    matId = s.matId;
    return true;
}
__device__ bool hit_plane(const PlaneCUDA& p, float3 ro, float3 rd, float& tHit, float3& nOut, int& matId) {
    float denom = dot3(p.normal, rd);
    if (fabsf(denom) < 1e-6f) return false;
    float t = -(dot3(p.normal, ro) + p.d) / denom;
    if (t <= 1e-4f) return false;
    tHit = t; nOut = normalize3(p.normal); matId = p.matId; return true;
}

// sampling
__device__ inline void basisFromNormal(const float3& N, float3& T, float3& B) {
    T = normalize3(fabsf(N.z) < 0.999f ? make_float3(-N.y, N.x, 0.f) : make_float3(0.f, 1.f, 0.f));
    T = normalize3(sub3(T, mul3(N, dot3(T,N))));                // orthonormalize T against N
    B = make_float3(N.y*T.z - N.z*T.y, N.z*T.x - N.x*T.z, N.x*T.y - N.y*T.x); // B = N x T
}
__device__ float3 cosineSampleHemisphere(RNG& rng, const float3& N) {
    float r1 = rng.f32();
    float r2 = rng.f32();
    float phi = 6.28318530718f * r1;
    float r = sqrtf(r2);
    float x = r * cosf(phi);
    float y = r * sinf(phi);
    float z = sqrtf(1.f - r2);
    float3 T,B; basisFromNormal(N, T, B);
    return normalize3(add3(add3(mul3(T,x), mul3(B,y)), mul3(N,z)));
}

__device__ float3 skyGradient(float3 rd) {
    float t = 0.5f*(rd.y + 1.0f);
    float3 top = make_float3(0.5f, 0.7f, 1.0f);
    float3 bot = make_float3(1.0f, 1.0f, 1.0f);
    return add3(mul3(bot, (1.0f - t)), mul3(top, t));
}

#ifndef SPP_PER_FRAME
#define SPP_PER_FRAME 1
#endif

static __device__ __host__ inline float _pi() { return 3.14159265358979323846f; }

struct SunCUDA {
    float3 dir;
    float3 color;
    float  cosThetaMax;
    float  solidAngle;
};

static __device__ inline SunCUDA makeSun(float3 dir, float3 color, float degrees) {
    float theta = degrees * 0.017453292519943295f; // deg->rad
    float c = cosf(theta);
    float sa = 2.0f * _pi() * (1.0f - c);
    SunCUDA s{ normalize3(dir), color, c, sa };
    return s;
}

static __device__ inline void basisFromDir(const float3& N, float3& T, float3& B) {
    float3 n = (fabsf(N.z) < 0.999f) ? make_float3(-N.y, N.x, 0.f) : make_float3(0.f, 1.f, 0.f);
    n = normalize3(sub3(n, mul3(N, dot3(n,N))));
    T = n;
    B = make_float3(N.y*T.z - N.z*T.y, N.z*T.x - N.x*T.z, N.x*T.y - N.y*T.x);
}

static __device__ inline float3 sampleSunDir(RNG& rng, const SunCUDA& sun, float& pdf) {
    float xi1 = rng.f32();
    float xi2 = rng.f32();
    float cosTheta = 1.0f - xi1 * (1.0f - sun.cosThetaMax);     // concentric on cone cap
    float sinTheta = sqrtf(fmaxf(0.f, 1.0f - cosTheta*cosTheta));
    float phi = 2.f * _pi() * xi2;

    float3 T, B; basisFromDir(sun.dir, T, B);
    float3 local = make_float3(cosf(phi)*sinTheta, sinf(phi)*sinTheta, cosTheta);
    float3 wi = normalize3(add3(add3(mul3(T, local.x), mul3(B, local.y)), mul3(sun.dir, local.z)));
    pdf = 1.0f / sun.solidAngle;                                 // uniform over cone area
    return wi;
}

// ACES tonemap
static __device__ inline float3 tonemapACES(float3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    float3 num = make_float3(x.x*(a*x.x + b), x.y*(a*x.y + b), x.z*(a*x.z + b));
    float3 den = make_float3(x.x*(c*x.x + d) + e, x.y*(c*x.y + d) + e, x.z*(c*x.z + d) + e);
    return make_float3(fminf(fmaxf(num.x/den.x, 0.f), 1.f),
                       fminf(fmaxf(num.y/den.y, 0.f), 1.f),
                       fminf(fmaxf(num.z/den.z, 0.f), 1.f));
}

static __device__ inline float luminance(float3 c){
    return 0.2126f*c.x + 0.7152f*c.y + 0.0722f*c.z;
}

struct HitInfo {
    float  t;
    float3 n;
    int    matId;
    bool   hit;
};

__device__ HitInfo traceClosest(
    float3 ro, float3 rd,
    const SphereCUDA* spheres, int numSpheres,
    const PlaneCUDA*  planes,  int numPlanes)
{
    HitInfo h; h.t = 1e20f; h.hit = false; h.n = make_float3(0,1,0); h.matId = 0;

    for (int i=0; i<numSpheres; ++i) {
        float tHit; float3 n; int mid;
        if (hit_sphere(spheres[i], ro, rd, tHit, n, mid) && tHit < h.t) {
            h.t = tHit; h.n = n; h.matId = mid; h.hit = true;
        }
    }
    for (int i=0; i<numPlanes; ++i) {
        float tHit; float3 n; int mid;
        if (hit_plane(planes[i], ro, rd, tHit, n, mid) && tHit < h.t) {
            h.t = tHit; h.n = n; h.matId = mid; h.hit = true;
        }
    }
    return h;
}

__global__ void pathtrace_kernel(
    uchar4* pixels, float4* accum,
    int width, int height,
    CameraCUDA cam,
    const SphereCUDA* spheres, int numSpheres,
    const PlaneCUDA* planes,  int numPlanes,
    const MaterialCUDA* materials, int numMaterials,
    int frameIndex, int maxDepth)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    int idx = y * width + x;

    SunCUDA sun = makeSun(normalize3(make_float3(0.6f, 1.0f, -0.4f)), // main light direction
                          make_float3(3.0f, 2.8f, 2.6f),              // light radiance
                          0.5f);                                      // angular radius in degrees

    // decorrelated seed per pixel per frame
    uint32_t baseSeed = 2166136261u ^ (x*16777619u) ^ (y*374761393u) ^ (frameIndex*668265263u);
    RNG rng(baseSeed);

    float3 Lsum = make_float3(0,0,0);

    for (int s = 0; s < SPP_PER_FRAME; ++s) {
        float jx = rng.f32() - 0.5f;  // subpixel jitter
        float jy = rng.f32() - 0.5f;

        float u = (2.0f * ((x + 0.5f + jx) / width) - 1.0f) * cam.fovScale * cam.aspect;
        float v = (1.0f - 2.0f * ((y + 0.5f + jy) / height)) * cam.fovScale;

        float3 rd = normalize3(add3(add3(cam.forward, mul3(cam.right, u)), mul3(cam.up, v)));
        float3 ro = cam.pos;

        float3 L = make_float3(0,0,0);
        float3 T = make_float3(1,1,1);

        for (int depth=0; depth<maxDepth; ++depth) {
            HitInfo h = traceClosest(ro, rd, spheres, numSpheres, planes, numPlanes);
            if (!h.hit) {
                L = add3(L, mul3(T, skyGradient(rd)));             // environment
                break;
            }

            float3 P = add3(ro, mul3(rd, h.t));
            const MaterialCUDA m = materials[(h.matId>=0 && h.matId<numMaterials)? h.matId : 0];
            float3 N = (dot3(h.n, rd) < 0) ? h.n : mul3(h.n, -1.f);

            if (m.emission.x>0 || m.emission.y>0 || m.emission.z>0) {
                L = add3(L, mul3(T, m.emission));                 // hit light
            }

            // next-event estimation for diffuse
            if (m.type == MAT_LAMBERT) {
                float lightPdf;
                float3 wiL = sampleSunDir(rng, sun, lightPdf);
                float cosNL = fmaxf(0.f, dot3(N, wiL));
                if (cosNL > 0.f) {
                    // shadow ray; any hit = blocked
                    float3 roL = add3(P, mul3(wiL, 1e-3f));       // offset to avoid self-hit
                    bool occluded = false;
                    for (int i=0;i<numSpheres && !occluded;++i) {
                        float tHit; float3 nTmp; int mTmp;
                        if (hit_sphere(spheres[i], roL, wiL, tHit, nTmp, mTmp)) { occluded = true; }
                    }
                    for (int i=0;i<numPlanes && !occluded;++i) {
                        float tHit; float3 nTmp; int mTmp;
                        if (hit_plane(planes[i], roL, wiL, tHit, nTmp, mTmp)) { occluded = true; }
                    }
                    if (!occluded) {
                        float bsdfPdf = cosNL / _pi();            // cosine / pi
                        float wLight  = (lightPdf*lightPdf) / (lightPdf*lightPdf + bsdfPdf*bsdfPdf); // power heuristic
                        float3 contrib = mul3(m.albedo, (cosNL / lightPdf));
                        contrib = mul3(contrib, sun.color);
                        contrib = mul3(contrib, wLight);
                        float Y = luminance(contrib);             // simple firefly clamp
                        const float maxY = 10.0f;
                        if (Y > maxY) contrib = mul3(contrib, maxY / (Y + 1e-6f));
                        L = add3(L, mul3(T, contrib));
                    }
                }
            }

            // path continuation
            if (m.type == MAT_LAMBERT) {
                rd = cosineSampleHemisphere(rng, N);
                ro = add3(P, mul3(rd, 1e-3f));
                T  = mul3(T, m.albedo);
            } else if (m.type == MAT_MIRROR) {
                rd = reflect3(rd, N);
                ro = add3(P, mul3(rd, 1e-3f));
                T  = mul3(T, m.albedo);
            } else {
                bool into = (dot3(h.n, rd) < 0.f);
                float3 Nn = into ? h.n : mul3(h.n, -1.f);
                float eta = into ? (1.f / fmaxf(m.eta, 1e-3f)) : fmaxf(m.eta, 1.0001f);
                float cosi = -dot3(rd, Nn);
                float3 wt;
                float Fr = refract3(rd, Nn, eta, wt) ? schlick(fabsf(cosi), eta) : 1.f;
                if (rng.f32() < Fr) {
                    rd = reflect3(rd, Nn);
                } else {
                    rd = normalize3(wt);
                }
                ro = add3(P, mul3(rd, 1e-3f));
            }

            // russian roulette
            if (depth >= 3) {
                float p = fmaxf(T.x, fmaxf(T.y, T.z));
                p = fminf(p, 0.99f);
                if (rng.f32() > p) break;
                T = mul3(T, 1.0f/p);
            }
        }

        Lsum = add3(Lsum, L);
    }

    float3 Lavg = mul3(Lsum, 1.0f / (float)SPP_PER_FRAME);

    // progressive accumulation
    float4 prev = accum[idx];
    float3 sum = make_float3(prev.x, prev.y, prev.z);
    float spp = prev.w;

    sum = add3(sum, Lavg);
    spp += 1.f;

    accum[idx] = make_float4(sum.x, sum.y, sum.z, spp);

    float3 avg = mul3(sum, 1.0f / spp);

    // tonemap + sRGB
    float3 tm  = tonemapACES(avg);
    pixels[idx] = make_uchar4(toSRGB8(tm.x), toSRGB8(tm.y), toSRGB8(tm.z), 255);
}

// host-side helpers
static SphereCUDA toDevice(const Sphere& s, int matId) {
    return {
        make_float3(s.center.x, s.center.y, s.center.z),
        (float)s.radius,
        matId
    };
}
static PlaneCUDA toDevice(const Plane& p, int matId) {
    return {
        make_float3(p.normal.x, p.normal.y, p.normal.z),
        (float)p.d,
        matId
    };
}
static CameraCUDA toDevice(const Camera& c, int width, int height) {
    float aspect = (float)width / (float)height;
    float fovScale = tanf(0.5f * (float)c.fov * 3.14159f/180.0f);
    return {
        make_float3(c.position.x, c.position.y, c.position.z),
        make_float3(c.forward().x, c.forward().y, c.forward().z),
        make_float3(c.right().x,   c.right().y,   c.right().z),
        make_float3(c.up().x,      c.up().y,      c.up().z),
        fovScale,
        aspect
    };
}
static MaterialCUDA makeLambert(float r, float g, float b) {
    MaterialCUDA m{};
    m.type   = MAT_LAMBERT;
    m.albedo = make_float3(r,g,b);
    m.eta    = 1.5f;
    m.emission = make_float3(0,0,0);
    return m;
}

CudaTracer::CudaTracer(unsigned int width, unsigned int height)
    : m_width(width), m_height(height) {}

void CudaTracer::init() {
    glGenBuffers(1, &m_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,
                 static_cast<GLsizeiptr>(m_width) * m_height * 4,
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    cudaGraphicsGLRegisterBuffer(&m_cudaPBO, m_pbo, cudaGraphicsMapFlagsWriteDiscard);

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

    cudaMalloc(&m_dAccum, sizeof(float4) * m_width * m_height);
    cudaMemset(m_dAccum, 0, sizeof(float4) * m_width * m_height);
    m_frameIndex = 0;
    m_hasPrevCam = false;
}

static inline bool camDiff(float a, float b) { return fabsf(a-b) > 1e-5f; }
static inline bool camChanged(const CudaTracer::CamSig& prev, bool hasPrev, const Camera& cam) {
    if (!hasPrev) return true;
    auto f = cam.forward(), r = cam.right(), u = cam.up();
    if (camDiff((float)cam.position.x, prev.pos[0]) ||
        camDiff((float)cam.position.y, prev.pos[1]) ||
        camDiff((float)cam.position.z, prev.pos[2])) return true;
    if (camDiff((float)f.x, prev.fwd[0]) || camDiff((float)f.y, prev.fwd[1]) || camDiff((float)f.z, prev.fwd[2])) return true;
    if (camDiff((float)r.x, prev.right[0]) || camDiff((float)r.y, prev.right[1]) || camDiff((float)r.z, prev.right[2])) return true;
    if (camDiff((float)u.x, prev.up[0]) || camDiff((float)u.y, prev.up[1]) || camDiff((float)u.z, prev.up[2])) return true;
    if (camDiff((float)cam.fov, prev.fov)) return true;
    return false;
}
static inline void cacheCam(CudaTracer::CamSig& dst, bool& hasPrev, const Camera& cam) {
    auto f = cam.forward(), r = cam.right(), u = cam.up();
    dst.pos[0]=(float)cam.position.x; dst.pos[1]=(float)cam.position.y; dst.pos[2]=(float)cam.position.z;
    dst.fwd[0]=(float)f.x; dst.fwd[1]=(float)f.y; dst.fwd[2]=(float)f.z;
    dst.right[0]=(float)r.x; dst.right[1]=(float)r.y; dst.right[2]=(float)r.z;
    dst.up[0]=(float)u.x; dst.up[1]=(float)u.y; dst.up[2]=(float)u.z;
    dst.fov=(float)cam.fov; hasPrev=true;
}

void CudaTracer::resetAccum() {
    if (!m_dAccum) return;
    cudaMemset(m_dAccum, 0, sizeof(float4) * m_width * m_height);
    m_frameIndex = 0;
}

void CudaTracer::drawFrame(Camera& cam, const Scene& scene) {
    if (camChanged(m_prevCam, m_hasPrevCam, cam)) resetAccum();
    cacheCam(m_prevCam, m_hasPrevCam, cam);

    // per-primitive lambert from scene colors
    std::vector<MaterialCUDA> hMats;
    hMats.reserve(scene.spheres.size() + scene.planes.size());

    std::vector<SphereCUDA> hS;
    hS.reserve(scene.spheres.size());
    for (auto& s : scene.spheres) {
        int matId = (int)hMats.size();
        hMats.push_back(makeLambert((float)s.color.r, (float)s.color.g, (float)s.color.b));
        hS.push_back(toDevice(s, matId));
    }

    std::vector<PlaneCUDA> hP;
    hP.reserve(scene.planes.size());
    for (auto& p : scene.planes) {
        int matId = (int)hMats.size();
        hMats.push_back(makeLambert((float)p.color.r, (float)p.color.g, (float)p.color.b));
        hP.push_back(toDevice(p, matId));
    }

    SphereCUDA*   dSpheres = nullptr;
    PlaneCUDA*    dPlanes  = nullptr;
    MaterialCUDA* dMats    = nullptr;

    if (!hS.empty())   cudaMalloc(&dSpheres, hS.size() * sizeof(SphereCUDA));
    if (!hP.empty())   cudaMalloc(&dPlanes,  hP.size() * sizeof(PlaneCUDA));
    if (!hMats.empty())cudaMalloc(&dMats,    hMats.size() * sizeof(MaterialCUDA));

    if (!hS.empty())   cudaMemcpy(dSpheres, hS.data(), hS.size()*sizeof(SphereCUDA), cudaMemcpyHostToDevice);
    if (!hP.empty())   cudaMemcpy(dPlanes,  hP.data(), hP.size()*sizeof(PlaneCUDA),  cudaMemcpyHostToDevice);
    if (!hMats.empty())cudaMemcpy(dMats,    hMats.data(), hMats.size()*sizeof(MaterialCUDA), cudaMemcpyHostToDevice);

    CameraCUDA dCam = toDevice(cam, m_width, m_height);

    uchar4* devPtr = nullptr; size_t size = 0;
    cudaGraphicsMapResources(1, &m_cudaPBO, 0);
    cudaGraphicsResourceGetMappedPointer((void**)&devPtr, &size, m_cudaPBO);


    dim3 block(16, 16);
    dim3 grid((m_width + 15) / 16, (m_height + 15) / 16);

    int maxDepth = 6;
    pathtrace_kernel<<<grid, block>>>(
        devPtr, m_dAccum,
        (int)m_width, (int)m_height,
        dCam,
        dSpheres, (int)hS.size(),
        dPlanes,  (int)hP.size(),
        dMats,    (int)hMats.size(),
        (int)m_frameIndex, maxDepth);

    cudaDeviceSynchronize();
    cudaGraphicsUnmapResources(1, &m_cudaPBO, 0);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
    glBindTexture(GL_TEXTURE_2D, m_glTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height),
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    if (dSpheres) cudaFree(dSpheres);
    if (dPlanes)  cudaFree(dPlanes);
    if (dMats)    cudaFree(dMats);

    ++m_frameIndex;
}

void CudaTracer::beginFrame() {}
void CudaTracer::endFrame() {}

void CudaTracer::resize(unsigned int w, unsigned int h) {
    if (w == 0 || h == 0) return;
    m_width = w; m_height = h;

    if (m_cudaPBO) { cudaGraphicsUnregisterResource(m_cudaPBO); m_cudaPBO = nullptr; }

    if (m_pbo) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,
                     static_cast<GLsizeiptr>(m_width) * m_height * 4,
                     nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    if (m_glTex) {
        glBindTexture(GL_TEXTURE_2D, m_glTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(m_width),
                     static_cast<GLsizei>(m_height),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    cudaGraphicsGLRegisterBuffer(&m_cudaPBO, m_pbo, cudaGraphicsMapFlagsWriteDiscard);

    if (m_dAccum) { cudaFree(m_dAccum); m_dAccum = nullptr; }
    cudaMalloc(&m_dAccum, sizeof(float4) * m_width * m_height);
    cudaMemset(m_dAccum, 0, sizeof(float4) * m_width * m_height);
    m_frameIndex = 0;
}

void CudaTracer::shutdown() {
    cleanup();
}

void CudaTracer::cleanup() {
    if (m_cudaPBO) {
        cudaGraphicsUnregisterResource(m_cudaPBO);
        m_cudaPBO = nullptr;
    }
    if (m_pbo) { glDeleteBuffers(1, &m_pbo); m_pbo = 0; }
    if (m_glTex) { glDeleteTextures(1, &m_glTex); m_glTex = 0; }
    if (m_dAccum) { cudaFree(m_dAccum); m_dAccum = nullptr; }
    m_frameIndex = 0; m_hasPrevCam = false;
}
